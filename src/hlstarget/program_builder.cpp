#include "hlstarget/program_builder.h"
#include <cassert>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "hlstarget/filter_builder.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"

namespace HLSTarget
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

static bool debug_opt = false;

bool ProgramBuilder::GenerateCode(StreamGraph::StreamGraph* streamgraph)
{
  CreateModule();

  if (!GenerateFilterFunctions(streamgraph))
    return false;

  if (debug_opt)
  {
    Log::Info("ProgramBuilder", "IR prior to optimization");
    m_context->DumpModule(m_module);
  }

  OptimizeModule();

  if (debug_opt)
  {
    Log::Info("ProgramBuilder", "IR after optimization");
    m_context->DumpModule(m_module);
  }

  return true;
}

void ProgramBuilder::CreateModule()
{
  m_module = m_context->CreateModule(m_module_name.c_str());
  Log::Info("HLSProgramBuilder", "Module name is '%s'", m_module_name.c_str());
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
  Log::Info("HLSTarget::ProgramBuilder", "Generating filter function set %s for %s", node->GetName().c_str(),
            node->GetFilterDeclaration()->GetName().c_str());

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
  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Join* node)
{
  return true;
}

bool ProgramBuilder::GenerateFilterFunctions(StreamGraph::StreamGraph* streamgraph)
{
  Log::Info("HLSProgramBuilder", "Generating filter and channel functions...");

  CodeGeneratorVisitor codegen(m_context, m_module);
  return streamgraph->GetRootNode()->Accept(&codegen);
}

void ProgramBuilder::OptimizeModule()
{
  Log::Info("ProgramBuilder", "Optimizing LLVM IR...");

  llvm::legacy::FunctionPassManager fpm(m_module);
  llvm::legacy::PassManager mpm;

  // Use standard -O2 optimizations, except disable loop unrolling.
  llvm::PassManagerBuilder builder;
  builder.OptLevel = 2;
  builder.DisableTailCalls = true;
  builder.DisableUnrollLoops = true;

  // Add loop unrolling passes afterwards with more aggressive parameters.
  builder.addExtension(llvm::PassManagerBuilder::EP_LoopOptimizerEnd,
                       [](const llvm::PassManagerBuilder& builder, llvm::legacy::PassManagerBase& pm) {
                         pm.add(llvm::createLoopUnrollPass(-1 /* threshold */, -1 /* count */, -1 /* allowpartial */,
                                                           -1 /* runtime */, -1 /* upperbound */));
                       });

  builder.populateFunctionPassManager(fpm);
  builder.populateModulePassManager(mpm);

  fpm.doInitialization();
  for (llvm::Function& F : *m_module)
    fpm.run(F);
  fpm.doFinalization();

  mpm.run(*m_module);
}

} // namespace HLSTarget
