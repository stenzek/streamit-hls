#pragma once
#include <unordered_map>
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

namespace Frontend
{
class Context;
class FilterFunctionBuilder;

class StatementBuilder : public AST::Visitor
{
public:
  StatementBuilder(FilterFunctionBuilder* func_builder);
  ~StatementBuilder();

  Context* GetContext() const;
  llvm::IRBuilder<>& GetIRBuilder() const;

  bool Visit(AST::Node* node) override;
  bool Visit(AST::ExpressionStatement* node) override;
  bool Visit(AST::IfStatement* node) override;
  bool Visit(AST::ForStatement* node) override;
  bool Visit(AST::BreakStatement* node) override;
  bool Visit(AST::ContinueStatement* node) override;
  bool Visit(AST::ReturnStatement* node) override;
  bool Visit(AST::PushStatement* node) override;

private:
  FilterFunctionBuilder* m_func_builder;
};
}