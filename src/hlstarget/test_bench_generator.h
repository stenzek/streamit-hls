#pragma once
#include <cstddef>
#include <map>
#include <memory>
#include <vector>

class WrappedLLVMContext;

namespace llvm
{
class BasicBlock;
class Constant;
class ExecutionEngine;
class Function;
class GlobalVariable;
class Module;
}

namespace AST
{
class FilterDeclaration;
}

namespace StreamGraph
{
class StreamGraph;
class FilterPermutation;
}

namespace HLSTarget
{

class TestBenchGenerator
{
public:
  using FilterFunctionMap = std::map<const StreamGraph::FilterPermutation*, llvm::Function*>;
  using FilterDataVector = std::vector<unsigned char>;
  using FilterDataMap = std::map<const StreamGraph::FilterPermutation*, FilterDataVector>;

  TestBenchGenerator(WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph, const std::string& module_name,
                     const std::string& out_dir);
  ~TestBenchGenerator();

  WrappedLLVMContext* GetContext() const { return m_context; }
  const std::string& GetModuleName() const { return m_module_name; }
  llvm::Module* GetModule() const { return m_module; }

  bool GenerateTestBenches();

private:
  void CreateModule();
  bool GenerateFilterFunctions();
  llvm::Function* GenerateFilterFunction(const StreamGraph::FilterPermutation* filter_decl);
  bool CreateExecutionEngine();
  void ExecuteFilters();
  bool GenerateTestBenchCCode();

  WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_stream_graph;
  std::string m_module_name;
  std::string m_output_directory;
  llvm::Module* m_module = nullptr;
  llvm::GlobalVariable* m_input_buffer_global = nullptr;
  llvm::GlobalVariable* m_input_buffer_pos_global = nullptr;
  llvm::GlobalVariable* m_output_buffer_global = nullptr;
  llvm::GlobalVariable* m_output_buffer_pos_global = nullptr;
  llvm::ExecutionEngine* m_execution_engine = nullptr;

  FilterFunctionMap m_filter_functions;
  FilterDataMap m_filter_input_data;
  FilterDataMap m_filter_output_data;
};

} // namespace CPUTarget