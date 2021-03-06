#pragma once
#include <cstddef>
#include <map>
#include <memory>

namespace llvm
{
class BasicBlock;
class Constant;
class Function;
class Module;
}

namespace AST
{
class FilterDeclaration;
}

namespace Frontend
{
class WrappedLLVMContext;
}

namespace StreamGraph
{
class StreamGraph;
class FilterPermutation;
}

namespace HLSTarget
{

class ProjectGenerator
{
public:
  ProjectGenerator(Frontend::WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
                   const std::string& module_name, const std::string& output_dir);
  ~ProjectGenerator();

  Frontend::WrappedLLVMContext* GetContext() const { return m_context; }
  const std::string& GetModuleName() const { return m_module_name; }
  const std::string& GetOutputDirectoryName() const { return m_output_dir; }
  llvm::Module* GetModule() const { return m_module; }

  // Generates the whole module.
  bool GenerateCode();

  // Generates HLS and Vivado projects.
  bool GenerateProject();

private:
  void CreateModule();
  bool GenerateFilterFunctions();
  void OptimizeModule();

  bool CleanOutputDirectory();
  bool WriteCCode();
  bool GenerateCTestBench();
  bool GenerateComponent();
  bool GenerateComponentTestBench();
  bool GenerateAXISComponent();
  bool WriteFIFOComponent();
  bool WriteHLSScript();
  bool WriteVivadoScript();

  Frontend::WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_streamgraph;
  std::string m_module_name;
  std::string m_output_dir;
  llvm::Module* m_module = nullptr;
  bool m_has_axis_component = false;

  using FilterFunctionMap = std::map<const StreamGraph::FilterPermutation*, llvm::Function*>;
  FilterFunctionMap m_filter_function_map;
};

} // namespace HLSTarget