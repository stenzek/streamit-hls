#include "hlstarget/test_bench_generator.h"
#include <cassert>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "common/types.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "frontend/function_builder.h"
#include "frontend/state_variables_builder.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(HLSTarget::TestBenchGenerator);

// TODO: Move this elsewhere
#if defined(_WIN32) || defined(__CYGWIN__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

constexpr size_t DATA_BUFFER_SIZE = 1024 * 1024;
extern "C" EXPORT byte test_bench_gen_input_buffer[DATA_BUFFER_SIZE];
extern "C" EXPORT u32 test_bench_gen_input_buffer_pos;
extern "C" EXPORT byte test_bench_gen_output_buffer[DATA_BUFFER_SIZE];
extern "C" EXPORT u32 test_bench_gen_output_buffer_pos;

namespace HLSTarget
{
TestBenchGenerator::TestBenchGenerator(WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
                                       const std::string& module_name, const std::string& out_dir)
  : m_context(context), m_stream_graph(streamgraph), m_module_name(module_name), m_output_directory(out_dir)
{
}

TestBenchGenerator::~TestBenchGenerator()
{
  delete m_module;
}

bool TestBenchGenerator::GenerateTestBenches()
{
  CreateModule();

  if (!GenerateFilterFunctions())
    return false;

  return true;
}

void TestBenchGenerator::CreateModule()
{
  m_module = m_context->CreateModule(m_module_name.c_str());
  Log_InfoPrintf("Module name is '%s'", m_module_name.c_str());

  // Create data buffers in the module.
  // When the JIT compiles this code, it will resolve the symbols defined above.
  // This is how we pass the data from the JIT back to us.
  llvm::Type* buffer_type = llvm::ArrayType::get(m_context->GetByteType(), DATA_BUFFER_SIZE);
  assert(buffer_type && "buffer type is a valid type");
  m_input_buffer_global = new llvm::GlobalVariable(*m_module, buffer_type, false, llvm::GlobalValue::ExternalLinkage,
                                                   nullptr, "test_bench_gen_input_buffer");
  m_input_buffer_pos_global =
    new llvm::GlobalVariable(*m_module, m_context->GetIntType(), false, llvm::GlobalVariable::ExternalLinkage, nullptr,
                             "test_bench_gen_input_buffer_pos");
  m_output_buffer_global = new llvm::GlobalVariable(*m_module, buffer_type, false, llvm::GlobalValue::ExternalLinkage,
                                                    nullptr, "test_bench_gen_output_buffer");
  m_output_buffer_pos_global =
    new llvm::GlobalVariable(*m_module, m_context->GetIntType(), false, llvm::GlobalVariable::ExternalLinkage, nullptr,
                             "test_bench_gen_output_buffer_pos");
}

bool TestBenchGenerator::GenerateFilterFunctions()
{
  auto filter_list = m_stream_graph->GetFilterPermutationList();
  if (filter_list.empty())
  {
    Log_ErrorPrint("No active filters in program");
    return false;
  }

  for (const auto& it : filter_list)
  {
    const AST::FilterDeclaration* filter_decl = it.first;

    // We only need a single filter function, since everything's going to be written to the global buffer.
    if (m_filter_functions.find(filter_decl) != m_filter_functions.end())
      continue;

    llvm::Function* func = GenerateFilterFunction(filter_decl);
    if (!func)
      return false;

    m_filter_functions.emplace(filter_decl, func);
  }

  Log_InfoPrintf("Generated %u filter functions", unsigned(m_filter_functions.size()));

  m_context->DumpModule(m_module);
  return true;
}

namespace
{
class FragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
public:
  FragmentBuilder(WrappedLLVMContext* context, llvm::GlobalVariable* input_buffer_var,
                  llvm::GlobalVariable* input_buffer_pos_var, llvm::GlobalVariable* output_buffer_var,
                  llvm::GlobalVariable* output_buffer_pos_var)
    : m_context(context), m_input_buffer_var(input_buffer_var), m_input_buffer_pos_var(input_buffer_pos_var),
      m_output_buffer_var(output_buffer_var), m_output_buffer_pos_var(output_buffer_pos_var)
  {
  }

  void CreateTemporaries(llvm::IRBuilder<>& builder)
  {
    // TODO: Fix this type
    m_temp = builder.CreateAlloca(m_context->GetIntType(), nullptr, "bitcast_temp");
  }

  // TODO: Fix this type
  u32 GetSizeOfElement() const { return sizeof(u32); }

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    // pop() is the same data as peek(0)
    llvm::Value* peek_val = BuildPeek(builder, builder.getInt32(0));
    // new_pos <- input_buffer_pos + sizeof(elem)
    llvm::Value* new_pos = builder.CreateAdd(builder.CreateLoad(m_input_buffer_pos_var, "input_buffer_pos"),
                                             builder.getInt32(GetSizeOfElement()));
    // input_buffer_pos <- new_pos
    builder.CreateStore(new_pos, m_input_buffer_pos_var);
    return peek_val;
  }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final
  {
    llvm::Type* byte_ptr_type = llvm::PointerType::getUnqual(m_context->GetByteType());
    // input_buffer_pos <- GLOBAL_input_buffer_pos
    llvm::Value* input_buffer_pos = builder.CreateLoad(m_input_buffer_pos_var, "input_buffer_pos");
    // input_buffer_ptr <- &GLOBAL_input_buffer[input_buffer_pos]
    llvm::Value* input_buffer_ptr =
      builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), input_buffer_pos}, "input_buffer_ptr");
    // bitcast_temp_ptr <- (i8*)&bitcast_temp
    llvm::Value* raw_ptr = builder.CreateBitCast(m_temp, byte_ptr_type, "bitcast_temp_ptr");
    // memcpy(bitcast_temp_ptr, input_buffer_ptr, sizeof(elem))
    builder.CreateMemCpy(raw_ptr, input_buffer_ptr, GetSizeOfElement(), GetSizeOfElement());
    // peek_val <- bitcast_temp
    return builder.CreateLoad(m_temp, "peek_val");
  }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    llvm::Type* byte_ptr_type = llvm::PointerType::getUnqual(m_context->GetByteType());
    // input_buffer_pos <- GLOBAL_input_buffer_pos
    llvm::Value* input_buffer_pos = builder.CreateLoad(m_input_buffer_pos_var, "input_buffer_pos");
    // input_buffer_ptr <- &GLOBAL_input_buffer[input_buffer_pos]
    llvm::Value* input_buffer_ptr =
      builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), input_buffer_pos}, "input_buffer_ptr");
    // bitcast_temp <- value
    builder.CreateStore(value, m_temp);
    // bitcast_temp_ptr <- (i8*)&value
    llvm::Value* raw_ptr = builder.CreateBitCast(m_temp, byte_ptr_type, "value_raw_ptr");
    // memcpy(bitcast_temp_ptr, input_buffer_ptr, sizeof(elem))
    builder.CreateMemCpy(input_buffer_ptr, raw_ptr, GetSizeOfElement(), GetSizeOfElement());
    // new_pos <- input_buffer_pos + sizeof(elem)
    llvm::Value* new_pos = builder.CreateAdd(builder.CreateLoad(m_input_buffer_pos_var, "input_buffer_pos"),
                                             builder.getInt32(GetSizeOfElement()));
    // input_buffer_pos <- new_pos
    builder.CreateStore(new_pos, m_input_buffer_pos_var);
    return true;
  }

private:
  WrappedLLVMContext* m_context;
  llvm::AllocaInst* m_temp;
  llvm::GlobalVariable* m_input_buffer_var;
  llvm::GlobalVariable* m_input_buffer_pos_var;
  llvm::GlobalVariable* m_output_buffer_var;
  llvm::GlobalVariable* m_output_buffer_pos_var;
};
}

llvm::Function* TestBenchGenerator::GenerateFilterFunction(const AST::FilterDeclaration* filter_decl)
{
  std::string instance_name = filter_decl->GetName();
  std::string function_name = StringFromFormat("%s_work", instance_name.c_str());
  llvm::FunctionType* func_type = llvm::FunctionType::get(m_context->GetVoidType(), {}, false);
  llvm::Constant* func_cons = m_module->getOrInsertFunction(function_name, func_type);
  llvm::Function* func = llvm::dyn_cast<llvm::Function>(func_cons);
  if (!func)
  {
    Log_ErrorPrintf("Could not create function '%s'", function_name.c_str());
    return nullptr;
  }

  FragmentBuilder fragment_builder(m_context, m_input_buffer_global, m_input_buffer_pos_global, m_output_buffer_global,
                                   m_output_buffer_pos_global);
  Frontend::FunctionBuilder function_builder(m_context, m_module, &fragment_builder, func);
  fragment_builder.CreateTemporaries(function_builder.GetCurrentIRBuilder());

  // Visit the state variable declarations, generating LLVM variables for them
  if (filter_decl->HasStateVariables())
  {
    Frontend::StateVariablesBuilder gvb(m_context, m_module, instance_name);
    if (!filter_decl->GetStateVariables()->Accept(&gvb))
      return nullptr;

    for (const auto& it : gvb.GetVariableMap())
      function_builder.AddGlobalVariable(it.first, it.second);
  }

  // Generate the actual function.
  Log_DevPrintf("Generating work function '%s' for filter '%s'", function_name.c_str(), filter_decl->GetName().c_str());
  assert(filter_decl->HasWorkBlock());
  if (!filter_decl->GetWorkBlock()->Accept(&function_builder))
    return nullptr;

  return func;
}

namespace
{

class FilterExecutorVisitor : public StreamGraph::Visitor
{
public:
  FilterExecutorVisitor(WrappedLLVMContext* context, const TestBenchGenerator::FilterFunctionMap& filter_functions)
    : m_context(context), m_filter_functions(filter_functions)
  {
  }

  virtual bool Visit(StreamGraph::Filter* node) override;
  virtual bool Visit(StreamGraph::Pipeline* node) override;
  virtual bool Visit(StreamGraph::SplitJoin* node) override;
  virtual bool Visit(StreamGraph::Split* node) override;
  virtual bool Visit(StreamGraph::Join* node) override;

private:
  WrappedLLVMContext* m_context;
  const TestBenchGenerator::FilterFunctionMap& m_filter_functions;
};

bool FilterExecutorVisitor::Visit(StreamGraph::Filter* node)
{
  Log_ErrorPrintf("TODO: Execute %s", node->GetName().c_str());
  return true;
}

bool FilterExecutorVisitor::Visit(StreamGraph::Pipeline* node)
{
  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

bool FilterExecutorVisitor::Visit(StreamGraph::SplitJoin* node)
{
  // TODO
  return true;
}

bool FilterExecutorVisitor::Visit(StreamGraph::Split* node)
{
  // TODO
  return true;
}

bool FilterExecutorVisitor::Visit(StreamGraph::Join* node)
{
  // TODO
  return true;
}

} // namespace

} // namespace CPUTarget
