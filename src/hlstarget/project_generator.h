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
class Module;
}

namespace StreamGraph
{
class StreamGraph;
}

namespace HLSTarget
{

class ProjectGenerator
{
public:
  ProjectGenerator(WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph, const std::string& module_name,
                   const std::string& output_dir);
  ~ProjectGenerator();

  WrappedLLVMContext* GetContext() const { return m_context; }
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
  bool GenerateTestBenches();
  bool WriteHLSScript();

  WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_streamgraph;
  std::string m_module_name;
  std::string m_output_dir;
  llvm::Module* m_module = nullptr;

  using FilterFunctionMap = std::map<std::string, llvm::Function*>;
  FilterFunctionMap m_filter_function_map;
};

} // namespace HLSTarget