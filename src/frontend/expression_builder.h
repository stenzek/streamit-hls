#pragma once
#include <unordered_map>
#include "llvm/IR/IRBuilder.h"
#include "parser/ast_visitor.h"

namespace Frontend
{
class FunctionBuilder;
class WrappedLLVMContext;

class ExpressionBuilder : public AST::Visitor
{
public:
  ExpressionBuilder(FunctionBuilder* func_builder);
  ~ExpressionBuilder();

  WrappedLLVMContext* GetContext() const;
  llvm::IRBuilder<>& GetIRBuilder() const;

  bool IsValid() const;
  bool IsPointer() const;
  llvm::Value* GetResultPtr();
  llvm::Value* GetResultValue();

  bool Visit(AST::Node* node) override;
  bool Visit(AST::IntegerLiteralExpression* node) override;
  bool Visit(AST::BooleanLiteralExpression* node) override;
  bool Visit(AST::FloatLiteralExpression* node) override;
  bool Visit(AST::IdentifierExpression* node) override;
  bool Visit(AST::IndexExpression* node) override;
  bool Visit(AST::CommaExpression* node) override;
  bool Visit(AST::AssignmentExpression* node) override;
  bool Visit(AST::UnaryExpression* node) override;
  bool Visit(AST::BinaryExpression* node) override;
  bool Visit(AST::RelationalExpression* node) override;
  bool Visit(AST::LogicalExpression* node) override;
  bool Visit(AST::PeekExpression* node) override;
  bool Visit(AST::PopExpression* node) override;
  bool Visit(AST::CallExpression* node) override;
  bool Visit(AST::CastExpression* node) override;

private:
  FunctionBuilder* m_func_builder;
  llvm::Value* m_result_ptr = nullptr;
  llvm::Value* m_result_value = nullptr;
};
}