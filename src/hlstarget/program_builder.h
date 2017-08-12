#pragma once
#include <cstddef>
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

class ProgramBuilder
{
public:
  ProgramBuilder(WrappedLLVMContext* context, const std::string& module_name);
  ~ProgramBuilder();

  WrappedLLVMContext* GetContext() const { return m_context; }
  const std::string& GetModuleName() const { return m_module_name; }
  llvm::Module* GetModule() const { return m_module; }

  // Transfers ownership to caller. Module will not be cleaned up.
  std::unique_ptr<llvm::Module> DetachModule();

  // Generates the whole module.
  bool GenerateCode(StreamGraph::StreamGraph* streamgraph);

private:
  void CreateModule();
  bool GenerateFilterFunctions(StreamGraph::StreamGraph* streamgraph);

  WrappedLLVMContext* m_context;
  std::string m_module_name;
  llvm::Module* m_module = nullptr;
};

} // namespace HLSTarget