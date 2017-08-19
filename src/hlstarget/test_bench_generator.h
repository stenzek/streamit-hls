#pragma once
#include <cstddef>
#include <map>
#include <memory>

class WrappedLLVMContext;

namespace llvm
{
class BasicBlock;
class Constant;
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
}

namespace HLSTarget
{

class TestBenchGenerator
{
public:
  using FilterFunctionMap = std::map<const AST::FilterDeclaration*, llvm::Function*>;

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
  llvm::Function* GenerateFilterFunction(const AST::FilterDeclaration* filter_decl);

  WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_stream_graph;
  std::string m_module_name;
  std::string m_output_directory;
  llvm::Module* m_module = nullptr;
  llvm::GlobalVariable* m_input_buffer_global = nullptr;
  llvm::GlobalVariable* m_input_buffer_pos_global = nullptr;
  llvm::GlobalVariable* m_output_buffer_global = nullptr;
  llvm::GlobalVariable* m_output_buffer_pos_global = nullptr;

  FilterFunctionMap m_filter_functions;
};

} // namespace CPUTarget