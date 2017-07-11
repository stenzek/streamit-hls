#pragma once
#include <unordered_map>
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

namespace Frontend
{
class Context;
class FilterFunctionBuilder;

class ExpressionBuilder : public AST::Visitor
{
public:
  ExpressionBuilder(FilterFunctionBuilder* func_builder);
  ~ExpressionBuilder();

  Context* GetContext() const;
  llvm::IRBuilder<>& GetIRBuilder() const;

  bool IsValid() const
  {
    return (m_result_value != nullptr);
  }
  llvm::Value* GetResultValue() const
  {
    return m_result_value;
  }

  bool Visit(AST::Node* node) override;
  bool Visit(AST::IntegerLiteralExpression* node) override;
  bool Visit(AST::BooleanLiteralExpression* node) override;
  bool Visit(AST::IdentifierExpression* node) override;
  bool Visit(AST::CommaExpression* node) override;
  bool Visit(AST::AssignmentExpression* node) override;
  bool Visit(AST::BinaryExpression* node) override;
  bool Visit(AST::RelationalExpression* node) override;
  bool Visit(AST::LogicalExpression* node) override;

private:
  FilterFunctionBuilder* m_func_builder;
  llvm::Value* m_result_value = nullptr;
};
}