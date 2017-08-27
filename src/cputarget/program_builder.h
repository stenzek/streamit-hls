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

namespace CPUTarget
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

  // Generates the whole program, entry point is main().
  bool GenerateCode(StreamGraph::StreamGraph* streamgraph);

  // Optimizes LLVM IR.
  void OptimizeModule();

private:
  void CreateModule();
  bool GenerateFilterAndChannelFunctions(StreamGraph::StreamGraph* streamgraph);
  bool GeneratePrimePumpFunction(StreamGraph::StreamGraph* streamgraph);
  bool GenerateSteadyStateFunction(StreamGraph::StreamGraph* streamgraph);
  bool GenerateMainFunction();

  // Returns the basic block after the loop exits
  llvm::BasicBlock* GenerateFunctionCalls(llvm::Function* func, llvm::BasicBlock* entry_bb,
                                          llvm::BasicBlock* current_bb, llvm::Constant* call_func, size_t count);

  WrappedLLVMContext* m_context;
  std::string m_module_name;
  llvm::Module* m_module = nullptr;
};

} // namespace CPUTarget