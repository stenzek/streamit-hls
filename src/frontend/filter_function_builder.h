#pragma once
#include <stack>
#include <unordered_map>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

namespace Frontend
{
class Context;
class FilterBuilder;

class FilterFunctionBuilder : public AST::Visitor
{
public:
  using VariableTable = std::unordered_map<const AST::VariableDeclaration*, llvm::Value*>;

  FilterFunctionBuilder(FilterBuilder* fb, const std::string& name, llvm::Function* func);
  ~FilterFunctionBuilder();

  bool Visit(AST::Node* node) override;
  bool Visit(AST::VariableDeclaration* node) override;
  bool Visit(AST::Statement* node) override;

  Context* GetContext() const;
  FilterBuilder* GetFilterBuilder() const;
  llvm::BasicBlock* GetEntryBasicBlock() const;
  llvm::BasicBlock* GetCurrentBasicBlock() const;
  llvm::IRBuilder<>& GetCurrentIRBuilder();

  void AddGlobalVariable(const AST::VariableDeclaration* var, llvm::GlobalVariable* gvar);
  llvm::AllocaInst* CreateVariable(const AST::VariableDeclaration* var);
  llvm::Value* GetVariablePtr(const AST::VariableDeclaration* var);
  llvm::Value* LoadVariable(const AST::VariableDeclaration* var);
  void StoreVariable(const AST::VariableDeclaration* var, llvm::Value* val);

  // Returns the old basic block pointer
  llvm::BasicBlock* NewBasicBlock(const std::string& name = {});

  // Switches to the specific basic block
  void SwitchBasicBlock(llvm::BasicBlock* new_bb);

  // break/continue basic block points - this is a stack
  llvm::BasicBlock* GetCurrentBreakBasicBlock() const;
  void PushBreakBasicBlock(llvm::BasicBlock* bb);
  void PopBreakBasicBlock();
  llvm::BasicBlock* GetCurrentContinueBasicBlock() const;
  void PushContinueBasicBlock(llvm::BasicBlock* bb);
  void PopContinueBasicBlock();

private:
  FilterBuilder* m_filter_builder;
  FilterFunctionBuilder* m_parent;
  std::string m_name;
  llvm::Function* m_func;
  llvm::BasicBlock* m_entry_basic_block;
  llvm::BasicBlock* m_current_basic_block;
  llvm::IRBuilder<> m_current_ir_builder;
  VariableTable m_vars;
  std::stack<llvm::BasicBlock*> m_break_basic_block_stack;
  std::stack<llvm::BasicBlock*> m_continue_basic_block_stack;
};
}