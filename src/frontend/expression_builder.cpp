#include "frontend/expression_builder.h"
#include <cassert>
#include "frontend/filter_function_builder.h"
#include "frontend/context.h"
#include "llvm/IR/Constants.h"
#include "parser/ast.h"

namespace Frontend
{
ExpressionBuilder::ExpressionBuilder(FilterFunctionBuilder* func_builder)
  : m_func_builder(func_builder)
{
}

ExpressionBuilder::~ExpressionBuilder()
{
}

Context* ExpressionBuilder::GetContext() const
{
  return m_func_builder->GetContext();
}

llvm::IRBuilder<>& ExpressionBuilder::GetIRBuilder() const
{
  return m_func_builder->GetCurrentIRBuilder();
}

bool ExpressionBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback handler executed");
  return false;
}

bool ExpressionBuilder::Visit(AST::IntegerLiteralExpression* node)
{
  llvm::Type* llvm_type = GetContext()->GetLLVMType(node->GetType());
  assert(llvm_type);

  m_result_value = llvm::ConstantInt::get(llvm_type, static_cast<uint64_t>(node->GetValue()), true);
  return IsValid();
}

bool ExpressionBuilder::Visit(AST::IdentifierExpression* node)
{
  m_result_value = m_func_builder->LoadVariable(node->GetReferencedVariable());
  return IsValid();
}

bool ExpressionBuilder::Visit(AST::AssignmentExpression* node)
{
  // Evaluate the expression, and return the result
  ExpressionBuilder eb(m_func_builder);
  if (!node->GetInnerExpression()->Accept(&eb) || !eb.IsValid())
    return false;

  m_func_builder->StoreVariable(node->GetReferencedVariable(), eb.GetResultValue());
  m_result_value = eb.GetResultValue();
  return m_result_value;
}
}