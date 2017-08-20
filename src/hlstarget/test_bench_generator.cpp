#include "hlstarget/test_bench_generator.h"
#include <cassert>
#include <iomanip>
#include <sstream>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "common/types.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "frontend/function_builder.h"
#include "frontend/state_variables_builder.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/TargetSelect.h"
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
EXPORT byte test_bench_gen_input_buffer[DATA_BUFFER_SIZE];
EXPORT u32 test_bench_gen_input_buffer_pos;
EXPORT byte test_bench_gen_output_buffer[DATA_BUFFER_SIZE];
EXPORT u32 test_bench_gen_output_buffer_pos;

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

  if (!CreateExecutionEngine())
    return false;

  ExecuteFilters();

  if (!GenerateTestBenchCCode())
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

  // m_context->DumpModule(m_module);
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
    llvm::Value* input_buffer_pos = builder.CreateLoad(m_output_buffer_pos_var, "input_buffer_pos");
    // input_buffer_ptr <- &GLOBAL_input_buffer[input_buffer_pos]
    llvm::Value* input_buffer_ptr =
      builder.CreateInBoundsGEP(m_output_buffer_var, {builder.getInt32(0), input_buffer_pos}, "input_buffer_ptr");
    // bitcast_temp <- value
    builder.CreateStore(value, m_temp);
    // bitcast_temp_ptr <- (i8*)&value
    llvm::Value* raw_ptr = builder.CreateBitCast(m_temp, byte_ptr_type, "value_raw_ptr");
    // memcpy(bitcast_temp_ptr, input_buffer_ptr, sizeof(elem))
    builder.CreateMemCpy(input_buffer_ptr, raw_ptr, GetSizeOfElement(), GetSizeOfElement());
    // new_pos <- input_buffer_pos + sizeof(elem)
    llvm::Value* new_pos = builder.CreateAdd(builder.CreateLoad(m_output_buffer_pos_var, "input_buffer_pos"),
                                             builder.getInt32(GetSizeOfElement()));
    // input_buffer_pos <- new_pos
    builder.CreateStore(new_pos, m_output_buffer_pos_var);
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

  // TODO: Need to insert the return. This should be fixed.
  function_builder.GetCurrentIRBuilder().CreateRetVoid();
  return func;
}

bool TestBenchGenerator::CreateExecutionEngine()
{
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string error_msg;
  m_execution_engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(m_module)).setErrorStr(&error_msg).create();

  if (!m_execution_engine)
  {
    Log_ErrorPrintf("Failed to create LLVM execution engine: %s", error_msg.c_str());
    return false;
  }

  m_module = nullptr;
  m_execution_engine->finalizeObject();
  return true;
}

namespace
{

class FilterExecutorVisitor : public StreamGraph::Visitor
{
public:
  FilterExecutorVisitor(WrappedLLVMContext* context, llvm::ExecutionEngine* execution_engine,
                        TestBenchGenerator::FilterDataMap& filter_input_data,
                        TestBenchGenerator::FilterDataMap& filter_output_data)
    : m_context(context), m_execution_engine(execution_engine), m_filter_input_data(filter_input_data),
      m_filter_output_data(filter_output_data)
  {
  }

  virtual bool Visit(StreamGraph::Filter* node) override;
  virtual bool Visit(StreamGraph::Pipeline* node) override;
  virtual bool Visit(StreamGraph::SplitJoin* node) override;
  virtual bool Visit(StreamGraph::Split* node) override;
  virtual bool Visit(StreamGraph::Join* node) override;

private:
  WrappedLLVMContext* m_context;
  llvm::ExecutionEngine* m_execution_engine;
  TestBenchGenerator::FilterDataMap& m_filter_input_data;
  TestBenchGenerator::FilterDataMap& m_filter_output_data;
};

bool FilterExecutorVisitor::Visit(StreamGraph::Filter* node)
{
  const AST::FilterDeclaration* filter_decl = node->GetFilterDeclaration();

  // Copy the last filter execution's output to this execution's input.
  test_bench_gen_input_buffer_pos = 0;
  if (!filter_decl->GetInputType()->IsVoid())
  {
    u32 input_data_size = test_bench_gen_output_buffer_pos;
    if (input_data_size > 0)
    {
      std::copy_n(test_bench_gen_output_buffer, input_data_size, test_bench_gen_input_buffer);
      std::fill_n(test_bench_gen_input_buffer + input_data_size, sizeof(test_bench_gen_input_buffer) - input_data_size,
                  0);
      Log_DevPrintf("Using %u bytes from previous filter", test_bench_gen_output_buffer_pos);
    }
    else
    {
      Log_ErrorPrintf("TODO: Add dummy input data.");
      input_data_size = 1024;
    }

    auto& data_vec = m_filter_input_data[filter_decl];
    data_vec.resize(input_data_size);
    std::copy_n(test_bench_gen_input_buffer, input_data_size, data_vec.begin());
  }

  // Ensure remaining data is zeroed out.
  std::fill_n(test_bench_gen_output_buffer, sizeof(test_bench_gen_output_buffer), 0);
  test_bench_gen_output_buffer_pos = 0;

  // Execute filter
  std::string function_name = StringFromFormat("%s_work", filter_decl->GetName().c_str());
  llvm::Function* filter_func = m_execution_engine->FindFunctionNamed(function_name);
  assert(filter_func && "filter function exists in execution engine");
  Log_DevPrintf("Executing filter '%s' %u times", function_name.c_str(), node->GetMultiplicity());
  for (u32 i = 0; i < node->GetMultiplicity(); i++)
    m_execution_engine->runFunction(filter_func, {});
  Log_DevPrintf("Exec result %u %u", test_bench_gen_input_buffer_pos, test_bench_gen_output_buffer_pos);

  // Do we have output data?
  if (!filter_decl->GetOutputType()->IsVoid())
  {
    std::string hexdump = HexDumpString(test_bench_gen_output_buffer, test_bench_gen_output_buffer_pos);
    Log_DevPrintf("Output data\n%s", hexdump.c_str());

    auto& data_vec = m_filter_output_data[filter_decl];
    data_vec.resize(test_bench_gen_output_buffer_pos);
    std::copy_n(test_bench_gen_output_buffer, test_bench_gen_output_buffer_pos, data_vec.begin());
  }

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

void TestBenchGenerator::ExecuteFilters()
{
  // Zero out buffers so we don't get any previous/undefined data.
  std::fill_n(test_bench_gen_input_buffer, sizeof(test_bench_gen_input_buffer), 0);
  std::fill_n(test_bench_gen_output_buffer, sizeof(test_bench_gen_output_buffer), 0);
  test_bench_gen_input_buffer_pos = 0;
  test_bench_gen_output_buffer_pos = 0;

  FilterExecutorVisitor visitor(m_context, m_execution_engine, m_filter_input_data, m_filter_output_data);
  m_stream_graph->GetRootNode()->Accept(&visitor);
}

static void WriteTestBenchFor(const AST::FilterDeclaration* filter_decl,
                              const TestBenchGenerator::FilterDataVector* input_data,
                              const TestBenchGenerator::FilterDataVector* output_data, llvm::raw_ostream& os)
{
  // TODO: Fix this for types...
  const char* in_type = "uint32_t";
  u32 in_element_size = sizeof(u32);
  u32 input_count = input_data ? (input_data->size() / in_element_size) : 0;
  u32 inputs_per_iteration = 0;
  const char* out_type = "uint32_t";
  u32 out_element_size = sizeof(u32);
  u32 output_count = output_data ? (output_data->size() / out_element_size) : 0;
  u32 outputs_per_iteration = 0;

  // Declaration of top function
  os << "void filter_" << filter_decl->GetName() << "(";
  if (input_data)
  {
    os << in_type << "*";
    inputs_per_iteration = filter_decl->GetWorkBlock()->GetPopRate();
    if (output_data)
    {
      os << ", " << out_type << "*";
      outputs_per_iteration = filter_decl->GetWorkBlock()->GetPushRate();
    }
  }
  else if (output_data)
  {
    os << out_type << "*";
    outputs_per_iteration = filter_decl->GetWorkBlock()->GetPushRate();
  }
  os << ");\n";

  // Definition of test function
  os << "int test_filter_" << filter_decl->GetName() << "() {\n";

  // Sometimes it doesn't compile in C99 mode...
  os << "  int i;\n";

  auto WriteData = [&](const char* array_name, const char* element_type, u32 element_size, u32 element_count,
                       const TestBenchGenerator::FilterDataVector* data) {
    const byte* current_in_ptr = data->data();
    os << "  " << in_type << " " << array_name << "[" << element_count << "] = {\n";
    for (u32 i = 0; i < element_count; i++)
    {
      u32 value;
      std::memcpy(&value, current_in_ptr, sizeof(value));
      current_in_ptr += element_size;
      os << "    " << llvm::format_hex(value, 10, true) << ",\n";
    }
    os << "  };\n";
  };

  // Write input data
  if (input_data)
    WriteData("INPUT_DATA", in_type, in_element_size, input_count, input_data);

  // Write expected output data
  if (output_data)
  {
    WriteData("REFERENCE_OUTPUT_DATA", out_type, out_element_size, output_count, output_data);
    os << "  " << out_type << " OUTPUT_DATA[" << output_count << "];\n";
  }

  u32 input_iterations = inputs_per_iteration ? input_count / inputs_per_iteration : 0;
  u32 output_iterations = outputs_per_iteration ? output_count / outputs_per_iteration : 0;
  u32 num_iterations = input_data ? input_iterations : output_iterations;
  if (input_data && output_data && input_iterations != output_iterations)
  {
    Log_ErrorPrintf("Iteration mismatch for '%s' (%u vs %u)", filter_decl->GetName().c_str(), input_iterations,
                    output_iterations);
  }

  // Print some info to the C file to improve readability
  os << "\n"
     << "  /* " << input_count << " inputs, " << inputs_per_iteration << " inputs per iteration */\n"
     << "  /* " << output_count << " outputs, " << outputs_per_iteration << " outputs per iteration */\n"
     << "  /* " << num_iterations << " iterations. */\n"
     << "\n";

  // Main execution loop
  if (input_data)
    os << "  " << in_type << "* current_in_ptr = INPUT_DATA;\n";
  if (output_data)
    os << "  " << out_type << "* current_out_ptr = OUTPUT_DATA;\n";

  os << "  for (i = 0; i < " << num_iterations << "; i++) {\n";
  os << "    filter_" << filter_decl->GetName() << "(";
  if (input_data)
  {
    os << "current_in_ptr";
    if (output_data)
      os << ", current_out_ptr";
  }
  else if (output_data)
  {
    os << "current_out_ptr";
  }
  os << ");\n";
  if (input_data)
    os << "    current_in_ptr += " << inputs_per_iteration << ";\n";
  if (output_data)
    os << "    current_out_ptr += " << outputs_per_iteration << ";\n";
  os << "  }\n";

  // Compare loop
  os << "  int num_errors = 0;\n";
  if (output_data)
  {
    os << "  for (i = 0; i < " << output_count << "; i++) {\n";
    os << "    if (OUTPUT_DATA[i] != REFERENCE_OUTPUT_DATA[i]) {\n";
    os << "      printf(\"Output mismatch for filter " << filter_decl->GetName() << " at output %d: %d vs %d\\n\",\n";
    os << "             i, OUTPUT_DATA[i], REFERENCE_OUTPUT_DATA[i]); \n";
    os << "      num_errors++;\n";
    os << "    }\n";
    os << "  }\n";
  }
  os << "  return num_errors;\n";
  os << "}\n";
  os << "\n";
}

bool TestBenchGenerator::GenerateTestBenchCCode()
{
  std::string c_filename = StringFromFormat("%s/hls/filters_tb.c", m_output_directory.c_str());
  Log_InfoPrintf("Writing test bench C code to %s...", c_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(c_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  // Write includes
  os << "#include <stdint.h>\n";
  os << "#include <stdio.h>\n";
  os << "#include <string.h>\n";
  os << "\n";

  // Write test functions
  auto filter_list = m_stream_graph->GetFilterPermutationList();
  std::vector<std::string> function_names;
  for (const auto& it : filter_list)
  {
    const AST::FilterDeclaration* filter_decl = it.first;
    const TestBenchGenerator::FilterDataVector* input_data = nullptr;
    const TestBenchGenerator::FilterDataVector* output_data = nullptr;
    auto iter = m_filter_input_data.find(filter_decl);
    if (iter != m_filter_input_data.end())
      input_data = &iter->second;
    iter = m_filter_output_data.find(filter_decl);
    if (iter != m_filter_output_data.end())
      output_data = &iter->second;

    if (!filter_decl->GetInputType()->IsVoid() && !input_data)
    {
      Log_ErrorPrintf("Missing input data for '%s'", filter_decl->GetName().c_str());
      continue;
    }
    if (!filter_decl->GetOutputType()->IsVoid() && !output_data)
    {
      Log_ErrorPrintf("Missing output data for '%s'", filter_decl->GetName().c_str());
      continue;
    }

    WriteTestBenchFor(filter_decl, input_data, output_data, os);
    function_names.push_back(filter_decl->GetName());
  }

  // Write main function
  os << "int main(int argc, char* argv[]) {\n";
  os << "  if (argc < 2) {\n";
  os << "    printf(\"No module specified.\\n\");\n";
  os << "    return 0;\n";
  os << "  }\n";
  os << "\n";
  os << "  const char* test_name = argv[1];\n";
  os << "\n";

  for (const auto& it : function_names)
  {
    os << "  if (!strcmp(test_name, \"filter_" << it << "\"))\n";
    os << "    return test_filter_" << it << "();\n";
  }

  os << "\n";
  os << "  printf(\"Unknown test name %s\\n\", test_name);\n";
  os << "  return 0;\n";
  os << "}\n";
  return true;
}

} // namespace CPUTarget
