#pragma once
#include <unordered_map>
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

namespace Frontend
{
class FunctionBuilder;
class WrappedLLVMContext;

class StatementBuilder : public AST::Visitor
{
public:
  StatementBuilder(FunctionBuilder* func_builder);
  ~StatementBuilder();

  WrappedLLVMContext* GetContext() const;
  llvm::Module* GetModule() const;
  llvm::IRBuilder<>& GetIRBuilder() const;

  bool Visit(AST::Node* node) override;
  bool Visit(AST::ExpressionStatement* node) override;
  bool Visit(AST::IfStatement* node) override;
  bool Visit(AST::ForStatement* node) override;
  bool Visit(AST::BreakStatement* node) override;
  bool Visit(AST::ContinueStatement* node) override;
  bool Visit(AST::ReturnStatement* node) override;
  bool Visit(AST::PushStatement* node) override;
  bool Visit(AST::AddStatement* node) override;
  bool Visit(AST::SplitStatement* node) override;
  bool Visit(AST::JoinStatement* node) override;

private:
  FunctionBuilder* m_func_builder;
};
}