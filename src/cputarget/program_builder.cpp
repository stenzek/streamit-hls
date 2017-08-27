#include "cputarget/program_builder.h"
#include <cassert>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "cputarget/channel_builder.h"
#include "cputarget/debug_print_builder.h"
#include "cputarget/filter_builder.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(CPUTarget::ProgramBuilder);

namespace CPUTarget
{
ProgramBuilder::ProgramBuilder(WrappedLLVMContext* context, const std::string& module_name)
  : m_context(context), m_module_name(module_name)
{
}

ProgramBuilder::~ProgramBuilder()
{
  delete m_module;
}

std::unique_ptr<llvm::Module> ProgramBuilder::DetachModule()
{
  if (!m_module)
    return nullptr;

  auto modptr = std::unique_ptr<llvm::Module>(m_module);
  m_module = nullptr;
  return std::move(modptr);
}

bool ProgramBuilder::GenerateCode(StreamGraph::StreamGraph* streamgraph)
{
  CreateModule();

  if (!GenerateFilterAndChannelFunctions(streamgraph))
    return false;

  if (!GeneratePrimePumpFunction(streamgraph))
    return false;

  if (!GenerateSteadyStateFunction(streamgraph))
    return false;

  if (!GenerateMainFunction())
    return false;

  return true;
}

void ProgramBuilder::CreateModule()
{
  m_module = m_context->CreateModule(m_module_name.c_str());
  Log_InfoPrintf("Module name is '%s'", m_module_name.c_str());
}

void ProgramBuilder::OptimizeModule()
{
  Log_InfoPrintf("Optimizing LLVM IR...");

  llvm::legacy::FunctionPassManager fpm(m_module);
  llvm::legacy::PassManager mpm;

  // Use standard -O2 optimizations.
  llvm::PassManagerBuilder builder;
  builder.OptLevel = 2;

  builder.populateFunctionPassManager(fpm);
  builder.populateModulePassManager(mpm);

  fpm.doInitialization();
  for (llvm::Function& F : *m_module)
    fpm.run(F);
  fpm.doFinalization();

  mpm.run(*m_module);
}

class CodeGeneratorVisitor : public StreamGraph::Visitor
{
public:
  CodeGeneratorVisitor(WrappedLLVMContext* context, llvm::Module* module) : m_context(context), m_module(module) {}

  virtual bool Visit(StreamGraph::Filter* node) override;
  virtual bool Visit(StreamGraph::Pipeline* node) override;
  virtual bool Visit(StreamGraph::SplitJoin* node) override;
  virtual bool Visit(StreamGraph::Split* node) override;
  virtual bool Visit(StreamGraph::Join* node) override;

private:
  WrappedLLVMContext* m_context;
  llvm::Module* m_module;
};

bool CodeGeneratorVisitor::Visit(StreamGraph::Filter* node)
{
  Log_InfoPrintf("Generating filter function set %s for %s", node->GetName().c_str(),
                 node->GetFilterPermutation()->GetFilterDeclaration()->GetName().c_str());

  // Generate fifo queue for the input side of this filter
  ChannelBuilder cb(m_context, m_module);
  if (!cb.GenerateCode(node))
    return false;

  // Generate functions for filter node
  FilterBuilder fb(m_context, m_module);
  if (!fb.GenerateCode(node))
    return false;

  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Pipeline* node)
{
  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::SplitJoin* node)
{
  if (!node->GetSplitNode()->Accept(this))
    return false;

  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  if (!node->GetJoinNode()->Accept(this))
    return false;

  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Split* node)
{
  ChannelBuilder cb(m_context, m_module);
  return cb.GenerateCode(node, 1);
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Join* node)
{
  ChannelBuilder cb(m_context, m_module);
  return cb.GenerateCode(node);
}

bool ProgramBuilder::GenerateFilterAndChannelFunctions(StreamGraph::StreamGraph* streamgraph)
{
  Log_InfoPrintf("Generating filter and channel functions...");

  CodeGeneratorVisitor codegen(m_context, m_module);
  return streamgraph->GetRootNode()->Accept(&codegen);
}

class FilterListVisitor : public StreamGraph::Visitor
{
public:
  using IterationPair = std::pair<u32, StreamGraph::Filter*>;
  using FilterList = std::vector<IterationPair>;

  FilterListVisitor() = default;

  const FilterList& GetFilterList() const { return m_filter_list; }

  virtual bool Visit(StreamGraph::Filter* node) override;
  virtual bool Visit(StreamGraph::Pipeline* node) override;
  virtual bool Visit(StreamGraph::SplitJoin* node) override;
  virtual bool Visit(StreamGraph::Split* node) override;
  virtual bool Visit(StreamGraph::Join* node) override;

private:
  FilterList m_filter_list;
  u32 m_current_iteration = 0;
};

bool FilterListVisitor::Visit(StreamGraph::Filter* node)
{
  m_filter_list.push_back(std::make_pair(m_current_iteration, node));
  m_current_iteration++;
  return true;
}

bool FilterListVisitor::Visit(StreamGraph::Pipeline* node)
{
  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

bool FilterListVisitor::Visit(StreamGraph::SplitJoin* node)
{
  u32 last_iteration = m_current_iteration;

  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;

    // All splitjoin children are executed in parallel
    m_current_iteration = last_iteration;
  }

  m_current_iteration = last_iteration + 1;
  return true;
}

bool FilterListVisitor::Visit(StreamGraph::Split* node)
{
  return true;
}

bool FilterListVisitor::Visit(StreamGraph::Join* node)
{
  return true;
}

bool ProgramBuilder::GeneratePrimePumpFunction(StreamGraph::StreamGraph* streamgraph)
{
  FilterListVisitor lv;
  if (!streamgraph->GetRootNode()->Accept(&lv))
    return false;

  Log_InfoPrintf("Generating prime pump function for %u filter instances...", unsigned(lv.GetFilterList().size()));
  for (auto ip : lv.GetFilterList())
  {
    Log_InfoPrintf("Iteration %u: %s (%u multiplicity)", ip.first, ip.second->GetName().c_str(),
                   ip.second->GetMultiplicity());
  }

  llvm::Constant* func_cons = m_module->getOrInsertFunction(StringFromFormat("%s_prime_pump", m_module_name.c_str()),
                                                            m_context->GetVoidType(), nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  func->setLinkage(llvm::GlobalValue::PrivateLinkage);

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);
  llvm::AllocaInst* iteration_var = builder.CreateAlloca(m_context->GetIntType(), nullptr, "iteration");
  builder.CreateStore(builder.getInt32(0), iteration_var);

  llvm::BasicBlock* start_loop_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
  llvm::BasicBlock* main_loop_bb = start_loop_bb;
  builder.CreateBr(main_loop_bb);

  // Get a list of iterations that have to be tested against.
  std::vector<u32> iteration_numbers;
  for (auto ip : lv.GetFilterList())
  {
    if (std::find(iteration_numbers.begin(), iteration_numbers.end(), ip.first) == iteration_numbers.end())
      iteration_numbers.push_back(ip.first);
  }

  for (u32 current_iteration : iteration_numbers)
  {
    llvm::BasicBlock* run_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
    llvm::BasicBlock* next_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);

    builder.SetInsertPoint(main_loop_bb);
    llvm::Value* iteration = builder.CreateLoad(iteration_var, "iteration");
    if (main_loop_bb == start_loop_bb)
      BuildDebugPrintf(m_context, builder, "prime iteration %d", {iteration});

    // if (iteration > #iteration#)
    llvm::Value* comp_res = builder.CreateICmpUGE(iteration, builder.getInt32(current_iteration));
    builder.CreateCondBr(comp_res, run_bb, next_bb);

    // Generate calls to work functions for all filters with a matching iteration
    for (auto ip2 : lv.GetFilterList())
    {
      if (ip2.first != current_iteration)
        continue;

      llvm::Constant* work_func = m_module->getOrInsertFunction(
        StringFromFormat("%s_work", ip2.second->GetName().c_str()), m_context->GetVoidType(), nullptr);
      if (!work_func)
        return false;

      run_bb = GenerateFunctionCalls(func, entry_bb, run_bb, work_func, ip2.second->GetMultiplicity());
    }

    builder.SetInsertPoint(run_bb);
    builder.CreateBr(next_bb);
    main_loop_bb = next_bb;
  }

  // if (iteration == #lastiteration#)
  //    return
  builder.SetInsertPoint(main_loop_bb);
  llvm::Value* iteration = builder.CreateLoad(iteration_var, "iteration");
  llvm::Value* comp_res =
    builder.CreateICmpEQ(iteration, builder.getInt32(lv.GetFilterList().back().first), "comp_res");
  llvm::BasicBlock* increment_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
  llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
  builder.CreateCondBr(comp_res, exit_bb, increment_bb);
  builder.SetInsertPoint(exit_bb);
  builder.CreateRetVoid();

  // iteration = iteration + 1
  builder.SetInsertPoint(increment_bb);
  iteration = builder.CreateLoad(iteration_var, "iteration");
  iteration = builder.CreateAdd(iteration, builder.getInt32(1), "iteration");
  builder.CreateStore(iteration, iteration_var);
  builder.CreateBr(start_loop_bb);
  return true;
}

bool ProgramBuilder::GenerateSteadyStateFunction(StreamGraph::StreamGraph* streamgraph)
{
  FilterListVisitor lv;
  if (!streamgraph->GetRootNode()->Accept(&lv))
    return false;

  Log_InfoPrintf("Generating steady state function for %u filter instances...", unsigned(lv.GetFilterList().size()));

  llvm::Constant* func_cons = m_module->getOrInsertFunction(StringFromFormat("%s_steady_state", m_module_name.c_str()),
                                                            m_context->GetVoidType(), nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  func->setLinkage(llvm::GlobalValue::PrivateLinkage);

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::BasicBlock* start_loop_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
  llvm::IRBuilder<> builder(entry_bb);
  builder.CreateBr(start_loop_bb);

  llvm::BasicBlock* main_loop_bb = start_loop_bb;
  for (auto ip : lv.GetFilterList())
  {
    // Call each filter multiplicity times.
    llvm::Constant* work_func = m_module->getOrInsertFunction(StringFromFormat("%s_work", ip.second->GetName().c_str()),
                                                              m_context->GetVoidType(), nullptr);
    if (!work_func)
      return false;
    main_loop_bb = GenerateFunctionCalls(func, entry_bb, main_loop_bb, work_func, ip.second->GetMultiplicity());
  }

  // Loop back to start infinitely.
  builder.SetInsertPoint(main_loop_bb);
  builder.CreateBr(start_loop_bb);
  return true;
}

bool ProgramBuilder::GenerateMainFunction()
{
  Log_InfoPrintf("Generating main function...");

  llvm::Constant* prime_pump_func = m_module->getOrInsertFunction(
    StringFromFormat("%s_prime_pump", m_module_name.c_str()), m_context->GetVoidType(), nullptr);
  llvm::Constant* steady_state_func = m_module->getOrInsertFunction(
    StringFromFormat("%s_steady_state", m_module_name.c_str()), m_context->GetVoidType(), nullptr);
  if (!prime_pump_func || !steady_state_func)
    return false;

  llvm::Constant* func_cons = m_module->getOrInsertFunction("main", m_context->GetIntType(), nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);
  BuildDebugPrint(m_context, builder, "Entering main");
  builder.CreateCall(prime_pump_func);
  builder.CreateCall(steady_state_func);
  builder.CreateRet(builder.getInt32(0));
  return true;
}

llvm::BasicBlock* ProgramBuilder::GenerateFunctionCalls(llvm::Function* func, llvm::BasicBlock* entry_bb,
                                                        llvm::BasicBlock* current_bb, llvm::Constant* call_func,
                                                        size_t count)
{
  llvm::IRBuilder<> builder(current_bb);

  // Don't generate a loop when there is only a single multiplicity filter.
  if (count == 1)
  {
    builder.CreateCall(call_func);
    return current_bb;
  }

  llvm::BasicBlock* compare_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
  llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);
  llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "", func);

  // int i = 0;
  builder.SetInsertPoint(entry_bb, entry_bb->begin());
  llvm::AllocaInst* i_var = builder.CreateAlloca(m_context->GetIntType(), nullptr, "i");
  builder.SetInsertPoint(current_bb);
  builder.CreateStore(builder.getInt32(0), i_var);
  builder.CreateBr(compare_bb);

  // compare:
  // if (i < count) goto body else goto exit
  builder.SetInsertPoint(compare_bb);
  llvm::Value* i = builder.CreateLoad(i_var, "i");
  llvm::Value* comp_res = builder.CreateICmpULT(i, builder.getInt32(count), "i_comp");
  builder.CreateCondBr(comp_res, body_bb, exit_bb);

  // func()
  builder.SetInsertPoint(body_bb);
  builder.CreateCall(call_func);

  // i = i + 1
  i = builder.CreateLoad(i_var, "i");
  i = builder.CreateAdd(i, builder.getInt32(1), "i");
  builder.CreateStore(i, i_var);

  // goto compare
  builder.CreateBr(compare_bb);

  // exit:
  return exit_bb;
}

} // namespace CPUTarget
