#pragma once
#include <unordered_map>
#include "common/types.h"

namespace llvm
{
class BasicBlock;
class Constant;
class Function;
class GlobalVariable;
class Module;
class Type;
}

namespace StreamGraph
{
class Node;
}

namespace Frontend
{
class Context;

class MainLoopBuilder
{
public:
  MainLoopBuilder(Context* context, llvm::Module* mod, const std::string& instance_name);
  ~MainLoopBuilder();

  Context* GetContext() const { return m_context; }

  // TODO: Move filters/channels generation to here, rename to ProgramBuilder?

  // Prime pump
  bool GeneratePrimePumpFunction(StreamGraph::Node* root_node);
  bool GenerateSteadyStateFunction(StreamGraph::Node* root_node);

private:
  // Returns the basic block after the loop exits
  llvm::BasicBlock* GenerateFunctionCalls(llvm::Function* func, llvm::BasicBlock* current_bb, llvm::Constant* call_func,
                                          u32 count);

  Context* m_context;
  llvm::Module* m_module;
  std::string m_instance_name;
};

} // namespace Frontend