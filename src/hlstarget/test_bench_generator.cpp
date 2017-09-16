#include "hlstarget/test_bench_generator.h"
#include <cassert>
#include <iomanip>
#include <sstream>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "common/types.h"
#include "frontend/function_builder.h"
#include "frontend/state_variables_builder.h"
#include "frontend/wrapped_llvm_context.h"
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
#include "llvm/IR/Type.h"
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
TestBenchGenerator::TestBenchGenerator(Frontend::WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
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
  for (const StreamGraph::FilterPermutation* filter_perm : m_stream_graph->GetFilterPermutationList())
  {
    llvm::Function* func = GenerateFilterFunction(filter_perm);
    if (!func)
      return false;

    m_filter_functions.emplace(filter_perm, func);
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
  FragmentBuilder(Frontend::WrappedLLVMContext* context, llvm::Type* input_type, llvm::GlobalVariable* input_buffer_var,
                  llvm::GlobalVariable* input_buffer_pos_var, llvm::Type* output_type,
                  llvm::GlobalVariable* output_buffer_var, llvm::GlobalVariable* output_buffer_pos_var)
    : m_context(context), m_input_type(input_type), m_input_buffer_var(input_buffer_var),
      m_input_buffer_pos_var(input_buffer_pos_var), m_output_type(output_type), m_output_buffer_var(output_buffer_var),
      m_output_buffer_pos_var(output_buffer_pos_var)
  {
  }

  void CreateTemporaries(llvm::IRBuilder<>& builder)
  {
    // TODO: Fix this type
    if (!m_input_type->isVoidTy())
      m_input_temp = builder.CreateAlloca(m_input_type, nullptr, "bitcast_input_temp");
    if (!m_output_type->isVoidTy())
      m_output_temp = builder.CreateAlloca(m_output_type, nullptr, "bitcast_output_temp");
  }

  u32 GetSizeOfInputElement() const
  {
    // Round up to nearest byte size.
    return (m_input_type->getPrimitiveSizeInBits() + 7) / 8;
  }

  u32 GetSizeOfOutputElement() const
  {
    // Round up to nearest byte size.
    return (m_output_type->getPrimitiveSizeInBits() + 7) / 8;
  }

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    // pop() is the same data as peek(0)
    llvm::Value* peek_val = BuildPeek(builder, builder.getInt32(0));
    // new_pos <- input_buffer_pos + sizeof(elem)
    llvm::Value* new_pos = builder.CreateAdd(builder.CreateLoad(m_input_buffer_pos_var, "input_buffer_pos"),
                                             builder.getInt32(GetSizeOfInputElement()));
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
    llvm::Value* raw_ptr = builder.CreateBitCast(m_input_temp, byte_ptr_type, "bitcast_temp_ptr");
    // memcpy(bitcast_temp_ptr, input_buffer_ptr, sizeof(elem))
    builder.CreateMemCpy(raw_ptr, input_buffer_ptr, GetSizeOfInputElement(), GetSizeOfInputElement());
    // peek_val <- bitcast_temp
    return builder.CreateLoad(m_input_temp, "peek_val");
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
    builder.CreateStore(value, m_output_temp);
    // bitcast_temp_ptr <- (i8*)&value
    llvm::Value* raw_ptr = builder.CreateBitCast(m_output_temp, byte_ptr_type, "value_raw_ptr");
    // memcpy(bitcast_temp_ptr, input_buffer_ptr, sizeof(elem))
    builder.CreateMemCpy(input_buffer_ptr, raw_ptr, GetSizeOfOutputElement(), GetSizeOfOutputElement());
    // new_pos <- input_buffer_pos + sizeof(elem)
    llvm::Value* new_pos = builder.CreateAdd(builder.CreateLoad(m_output_buffer_pos_var, "input_buffer_pos"),
                                             builder.getInt32(GetSizeOfOutputElement()));
    // input_buffer_pos <- new_pos
    builder.CreateStore(new_pos, m_output_buffer_pos_var);
    return true;
  }

private:
  Frontend::WrappedLLVMContext* m_context;
  llvm::AllocaInst* m_input_temp = nullptr;
  llvm::AllocaInst* m_output_temp = nullptr;
  llvm::Type* m_input_type;
  llvm::GlobalVariable* m_input_buffer_var;
  llvm::GlobalVariable* m_input_buffer_pos_var;
  llvm::Type* m_output_type;
  llvm::GlobalVariable* m_output_buffer_var;
  llvm::GlobalVariable* m_output_buffer_pos_var;
};
}

llvm::Function* TestBenchGenerator::GenerateFilterFunction(const StreamGraph::FilterPermutation* filter_perm)
{
  std::string function_name = StringFromFormat("%s_work", filter_perm->GetName().c_str());
  llvm::FunctionType* func_type = llvm::FunctionType::get(m_context->GetVoidType(), {}, false);
  llvm::Constant* func_cons = m_module->getOrInsertFunction(function_name, func_type);
  llvm::Function* func = llvm::dyn_cast<llvm::Function>(func_cons);
  if (!func)
  {
    Log_ErrorPrintf("Could not create function '%s'", function_name.c_str());
    return nullptr;
  }

  FragmentBuilder fragment_builder(m_context, filter_perm->GetInputType(), m_input_buffer_global,
                                   m_input_buffer_pos_global, filter_perm->GetOutputType(), m_output_buffer_global,
                                   m_output_buffer_pos_global);
  Frontend::FunctionBuilder function_builder(m_context, m_module, &fragment_builder, func);
  fragment_builder.CreateTemporaries(function_builder.GetCurrentIRBuilder());

  // Visit the state variable declarations, generating LLVM variables for them
  if (filter_perm->GetFilterDeclaration()->HasStateVariables())
  {
    Frontend::StateVariablesBuilder gvb(m_context, m_module, filter_perm->GetName());
    if (!filter_perm->GetFilterDeclaration()->GetStateVariables()->Accept(&gvb))
      return nullptr;

    for (const auto& it : gvb.GetVariableMap())
      function_builder.AddVariable(it.first, it.second);
  }

  // Generate the actual function.
  Log_DevPrintf("Generating work function '%s' for filter '%s'", function_name.c_str(), filter_perm->GetName().c_str());
  assert(filter_perm->GetFilterDeclaration()->HasWorkBlock());
  if (!filter_perm->GetFilterDeclaration()->GetWorkBlock()->Accept(&function_builder))
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
  FilterExecutorVisitor(Frontend::WrappedLLVMContext* context, llvm::ExecutionEngine* execution_engine,
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
  Frontend::WrappedLLVMContext* m_context;
  llvm::ExecutionEngine* m_execution_engine;
  TestBenchGenerator::FilterDataMap& m_filter_input_data;
  TestBenchGenerator::FilterDataMap& m_filter_output_data;
};

bool FilterExecutorVisitor::Visit(StreamGraph::Filter* node)
{
  const StreamGraph::FilterPermutation* filter_perm = node->GetFilterPermutation();

  // Copy the last filter execution's output to this execution's input.
  test_bench_gen_input_buffer_pos = 0;
  if (!filter_perm->GetInputType()->isVoidTy())
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

    auto& data_vec = m_filter_input_data[filter_perm];
    data_vec.resize(input_data_size);
    std::copy_n(test_bench_gen_input_buffer, input_data_size, data_vec.begin());
  }

  // Ensure remaining data is zeroed out.
  std::fill_n(test_bench_gen_output_buffer, sizeof(test_bench_gen_output_buffer), 0);
  test_bench_gen_output_buffer_pos = 0;

  // Execute filter
  std::string function_name = StringFromFormat("%s_work", filter_perm->GetName().c_str());
  llvm::Function* filter_func = m_execution_engine->FindFunctionNamed(function_name);
  assert(filter_func && "filter function exists in execution engine");
  Log_DevPrintf("Executing filter '%s' %u times", function_name.c_str(), node->GetMultiplicity());
  for (u32 i = 0; i < node->GetMultiplicity(); i++)
    m_execution_engine->runFunction(filter_func, {});
  Log_DevPrintf("Exec result %u %u", test_bench_gen_input_buffer_pos, test_bench_gen_output_buffer_pos);

  // Do we have output data?
  if (!filter_perm->GetOutputType()->isVoidTy())
  {
    std::string hexdump = HexDumpString(test_bench_gen_output_buffer, test_bench_gen_output_buffer_pos);
    Log_DevPrintf("Output data\n%s", hexdump.c_str());

    auto& data_vec = m_filter_output_data[filter_perm];
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

static std::string GetTestBenchTypeName(const llvm::Type* ty)
{
  if (ty->isIntegerTy())
  {
    const llvm::IntegerType* int_ty = llvm::cast<const llvm::IntegerType>(ty);
    if (int_ty->getBitWidth() == 1)
      return "uint1";
    else if (int_ty->getBitWidth() == 8)
      return "uint8_t";
    else if (int_ty->getBitWidth() == 16)
      return "uint16_t";
    else if (int_ty->getBitWidth() == 32)
      return "uint32_t";
    else
      return StringFromFormat("uint%u", int_ty->getBitWidth());
  }
  else if (ty->isFloatTy())
  {
    return "float";
  }
  else if (ty->isDoubleTy())
  {
    return "double";
  }
  else if (ty->isVoidTy())
  {
    return "void";
  }
  else
  {
    assert(0 && "unknown type");
    return "";
  }
}

static u32 GetTestBenchTypeSize(const llvm::Type* ty)
{
  // Round up to nearest byte.
  if (ty->isVoidTy())
    return 0;

  return (ty->getPrimitiveSizeInBits() + 7) / 8;
}

static void WriteTestBenchFor(const StreamGraph::FilterPermutation* filter_perm,
                              const TestBenchGenerator::FilterDataVector* input_data,
                              const TestBenchGenerator::FilterDataVector* output_data, llvm::raw_ostream& os)
{
  // TODO: Fix this for types...
  const std::string in_type = GetTestBenchTypeName(filter_perm->GetInputType());
  u32 in_element_size = GetTestBenchTypeSize(filter_perm->GetInputType());
  u32 input_count = input_data ? (input_data->size() / in_element_size) : 0;
  u32 inputs_per_iteration = 0;
  const std::string out_type = GetTestBenchTypeName(filter_perm->GetOutputType());
  u32 out_element_size = GetTestBenchTypeSize(filter_perm->GetOutputType());
  u32 output_count = output_data ? (output_data->size() / out_element_size) : 0;
  u32 outputs_per_iteration = 0;

  // Declaration of top function
  if (!filter_perm->IsCombinational())
  {
    os << "void filter_" << filter_perm->GetName() << "(";
    if (input_data)
    {
      os << in_type << "*";
      inputs_per_iteration = filter_perm->GetPopRate();
      if (output_data)
      {
        os << ", " << out_type << "*";
        outputs_per_iteration = filter_perm->GetPushRate();
      }
    }
    else if (output_data)
    {
      os << out_type << "*";
      outputs_per_iteration = filter_perm->GetPushRate();
    }
    os << ");\n";
  }
  else
  {
    if (output_data)
    {
      os << out_type << " ";
      outputs_per_iteration = filter_perm->GetPushRate();
    }
    else
    {
      os << out_type << "void ";
    }
    os << "filter_" << filter_perm->GetName() << "(";
    if (input_data)
    {
      os << in_type;
      inputs_per_iteration = filter_perm->GetPopRate();
    }
    os << ");\n";
  }

  // Definition of test function
  os << "int test_filter_" << filter_perm->GetName() << "() {\n";

  // Sometimes it doesn't compile in C99 mode...
  os << "  int i;\n";

  auto WriteData = [&](const char* array_name, const llvm::Type* element_type, const std::string& element_type_name,
                       u32 element_size, u32 element_count, const TestBenchGenerator::FilterDataVector* data) {
    const byte* current_in_ptr = data->data();
    os << "  " << element_type_name << " " << array_name << "[" << element_count << "] = {\n";
    for (u32 i = 0; i < element_count; i++)
    {
      // This will only work on little-endian..
      u32 value = 0;
      std::memcpy(&value, current_in_ptr, element_size);
      current_in_ptr += element_size;
      os << "    " << llvm::format_hex(value, 10, true);
      if (element_type->isIntegerTy())
        os << " & " << llvm::cast<const llvm::IntegerType>(element_type)->getBitMask();
      os << ",\n";
    }
    os << "  };\n";
  };

  // Write input data
  if (input_data)
    WriteData("INPUT_DATA", filter_perm->GetInputType(), in_type, in_element_size, input_count, input_data);

  // Write expected output data
  if (output_data)
  {
    WriteData("REFERENCE_OUTPUT_DATA", filter_perm->GetOutputType(), out_type, out_element_size, output_count,
              output_data);
    os << "  " << out_type << " OUTPUT_DATA[" << output_count << "];\n";
  }

  u32 input_iterations = inputs_per_iteration ? input_count / inputs_per_iteration : 0;
  u32 output_iterations = outputs_per_iteration ? output_count / outputs_per_iteration : 0;
  u32 num_iterations = input_data ? input_iterations : output_iterations;
  if (input_data && output_data && input_iterations != output_iterations)
  {
    Log_ErrorPrintf("Iteration mismatch for '%s' (%u vs %u)", filter_perm->GetName().c_str(), input_iterations,
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
  if (!filter_perm->IsCombinational())
  {
    os << "    filter_" << filter_perm->GetName() << "(";
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
  }
  else
  {
    os << "    ";
    if (output_data)
      os << "*current_out_ptr = ";
    os << "filter_" << filter_perm->GetName() << "(";
    if (input_data)
      os << "*current_in_ptr";
    os << ");\n";
  }

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
    os << "      printf(\"Output mismatch for filter " << filter_perm->GetName() << " at output %d: %d vs %d\\n\",\n";
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
  std::string c_filename = StringFromFormat("%s/_autogen_hls/filters_tb.c", m_output_directory.c_str());
  Log_InfoPrintf("Writing test bench C code to %s...", c_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(c_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  // Write includes
  os << "#include <stdint.h>\n";
  os << "#include <stdio.h>\n";
  os << "#include <string.h>\n";
  os << "#include <ap_cint.h>\n";
  os << "\n";

  // Write test functions
  std::vector<std::string> function_names;
  for (const StreamGraph::FilterPermutation* filter_perm : m_stream_graph->GetFilterPermutationList())
  {
    const TestBenchGenerator::FilterDataVector* input_data = nullptr;
    const TestBenchGenerator::FilterDataVector* output_data = nullptr;
    auto iter = m_filter_input_data.find(filter_perm);
    if (iter != m_filter_input_data.end())
      input_data = &iter->second;
    iter = m_filter_output_data.find(filter_perm);
    if (iter != m_filter_output_data.end())
      output_data = &iter->second;

    if (!filter_perm->GetInputType()->isVoidTy() && !input_data)
    {
      Log_ErrorPrintf("Missing input data for '%s'", filter_perm->GetName().c_str());
      continue;
    }
    if (!filter_perm->GetOutputType()->isVoidTy() && !output_data)
    {
      Log_ErrorPrintf("Missing output data for '%s'", filter_perm->GetName().c_str());
      continue;
    }

    WriteTestBenchFor(filter_perm, input_data, output_data, os);
    function_names.push_back(filter_perm->GetName());
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
