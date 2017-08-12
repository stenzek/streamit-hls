#pragma once
#include <stack>
#include <unordered_map>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

class WrappedLLVMContext;

namespace AST
{
class VariableDeclaration;
}

namespace Frontend
{
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

private:
  WrappedLLVMContext* m_context;
  llvm::Module* m_module;
  std::string m_prefix;
  VariableMap m_global_var_map;
};
}