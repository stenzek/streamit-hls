#include "frontend/expression_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/filter_function_builder.h"
#include "llvm/IR/Constants.h"
#include "parser/ast.h"
#include "parser/type.h"

namespace Frontend
{
ExpressionBuilder::ExpressionBuilder(FilterFunctionBuilder* func_builder) : m_func_builder(func_builder)
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

bool ExpressionBuilder::Visit(AST::BooleanLiteralExpression* node)
{
  llvm::Type* llvm_type = GetContext()->GetLLVMType(node->GetType());
  assert(llvm_type);

  m_result_value = llvm::ConstantInt::get(llvm_type, node->GetValue() ? 1 : 0);
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

bool ExpressionBuilder::Visit(AST::BinaryExpression* node)
{
  // Evaluate both lhs and rhs first (left-to-right?)
  ExpressionBuilder eb_lhs(m_func_builder);
  ExpressionBuilder eb_rhs(m_func_builder);
  if (!node->GetLHSExpression()->Accept(&eb_lhs) || !eb_lhs.IsValid() || !node->GetRHSExpression()->Accept(&eb_rhs) ||
      !eb_rhs.IsValid())
  {
    return false;
  }

  // TODO: Float operands
  if (node->GetType() == Type::GetIntType())
  {
    // TODO: Type conversion where LHS type != RHS type
    assert(node->GetLHSExpression()->GetType() == node->GetType() &&
           node->GetRHSExpression()->GetType() == node->GetType());

    llvm::Value* lhs_val = eb_lhs.GetResultValue();
    llvm::Value* rhs_val = eb_rhs.GetResultValue();
    switch (node->GetOperator())
    {
    case AST::BinaryExpression::Add:
      m_result_value = GetIRBuilder().CreateAdd(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::Subtract:
      m_result_value = GetIRBuilder().CreateSub(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::Multiply:
      m_result_value = GetIRBuilder().CreateMul(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::Divide:
      m_result_value = GetIRBuilder().CreateSDiv(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::Modulo:
      m_result_value = GetIRBuilder().CreateSRem(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::BitwiseAnd:
      m_result_value = GetIRBuilder().CreateAnd(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::BitwiseOr:
      m_result_value = GetIRBuilder().CreateOr(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::BitwiseXor:
      m_result_value = GetIRBuilder().CreateXor(lhs_val, rhs_val);
      break;
    default:
      assert(0 && "not reachable");
      break;
    }
  }

  return IsValid();
}

bool ExpressionBuilder::Visit(AST::RelationalExpression* node)
{
  // Evaluate both lhs and rhs first (left-to-right?)
  ExpressionBuilder eb_lhs(m_func_builder);
  ExpressionBuilder eb_rhs(m_func_builder);
  if (!node->GetLHSExpression()->Accept(&eb_lhs) || !eb_lhs.IsValid() || !node->GetRHSExpression()->Accept(&eb_rhs) ||
      !eb_rhs.IsValid())
  {
    return false;
  }

  // TODO: Float operands
  if (node->GetIntermediateType() == Type::GetIntType())
  {
    // TODO: Type conversion where LHS type != RHS type
    assert(node->GetLHSExpression()->GetType() == node->GetIntermediateType() &&
           node->GetRHSExpression()->GetType() == node->GetIntermediateType());

    llvm::Value* lhs_val = eb_lhs.GetResultValue();
    llvm::Value* rhs_val = eb_rhs.GetResultValue();
    switch (node->GetOperator())
    {
    case AST::RelationalExpression::Less:
      m_result_value = GetIRBuilder().CreateICmpSLT(lhs_val, rhs_val);
      break;
    case AST::RelationalExpression::LessEqual:
      m_result_value = GetIRBuilder().CreateICmpSLE(lhs_val, rhs_val);
      break;
    case AST::RelationalExpression::Greater:
      m_result_value = GetIRBuilder().CreateICmpSGT(lhs_val, rhs_val);
      break;
    case AST::RelationalExpression::GreaterEqual:
      m_result_value = GetIRBuilder().CreateICmpSGE(lhs_val, rhs_val);
      break;
    case AST::RelationalExpression::Equal:
      m_result_value = GetIRBuilder().CreateICmpEQ(lhs_val, rhs_val);
      break;
    case AST::RelationalExpression::NotEqual:
      m_result_value = GetIRBuilder().CreateICmpNE(lhs_val, rhs_val);
      break;
    default:
      assert(0 && "not reachable");
      break;
    }
  }

  return IsValid();
}
}