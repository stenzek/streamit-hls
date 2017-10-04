#pragma once
#include <memory>
#include <stack>
#include <unordered_map>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

namespace AST
{
class VariableDeclaration;
class FilterWorkBlock;
}

namespace llvm
{
class ExecutionEngine;
class Function;
class GlobalVariable;
class Module;
}

namespace StreamGraph
{
class FilterPermutation;
}

namespace Frontend
{
class WrappedLLVMContext;

/// Creates LLVM global variables for each filter state variable
/// Also creates initializers where they are constant
class StateVariablesBuilder : public AST::Visitor
{
public:
  using VariableMap = std::unordered_map<const AST::VariableDeclaration*, llvm::GlobalVariable*>;

  StateVariablesBuilder(WrappedLLVMContext* context_, llvm::Module* mod_, const std::string& prefix_);

  WrappedLLVMContext* GetContext() const { return m_context; }
  llvm::Module* GetModule() const { return m_module; }
  const std::string& GetPrefix() const { return m_prefix; }
  const VariableMap& GetVariableMap() const { return m_global_var_map; }

  bool Visit(AST::Node* node) override;
  bool Visit(AST::VariableDeclaration* node) override;

  // Evaluates variables in init block.
  bool EvaluateInitBlock(const StreamGraph::FilterPermutation* filter_perm);

private:
  llvm::Function* CompileInitBlock(const StreamGraph::FilterPermutation* filter_perm, llvm::Module* mod,
                                   VariableMap& gvm);
  std::unique_ptr<llvm::ExecutionEngine> ExecuteInitBlock(std::unique_ptr<llvm::Module> mod, llvm::Function* func);
  bool ReadbackInitBlock(const VariableMap& gvm, llvm::ExecutionEngine* execution_engine, bool stateless);

  WrappedLLVMContext* m_context;
  llvm::Module* m_module;
  std::string m_prefix;
  VariableMap m_global_var_map;
};
}