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

bool ExpressionBuilder::Visit(AST::CommaExpression* node)
{
  // Evaluate both sides, discard the left, and return the right.
  ExpressionBuilder eb_lhs(m_func_builder);
  ExpressionBuilder eb_rhs(m_func_builder);
  if (!node->GetLHSExpression()->Accept(&eb_lhs) || !eb_lhs.IsValid() || !node->GetLHSExpression()->Accept(&eb_rhs) ||
      !eb_rhs.IsValid())
  {
    return false;
  }

  m_result_value = eb_rhs.GetResultValue();
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
  return IsValid();
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
    case AST::BinaryExpression::LeftShift:
      m_result_value = GetIRBuilder().CreateShl(lhs_val, rhs_val);
      break;
    case AST::BinaryExpression::RightShift:
      m_result_value = GetIRBuilder().CreateAShr(lhs_val, rhs_val);
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

bool ExpressionBuilder::Visit(AST::LogicalExpression* node)
{
  // Evaluate left-hand side first, always, in the parent block
  ExpressionBuilder eb_lhs(m_func_builder);
  if (!node->GetLHSExpression()->Accept(&eb_lhs) || !eb_lhs.IsValid())
    return false;

  // Both should be boolean types.
  assert(node->GetLHSExpression()->GetType() == Type::GetBooleanType() &&
         node->GetRHSExpression()->GetType() == Type::GetBooleanType());

  // Create a new block for the right-hand side of the expression
  llvm::BasicBlock* lhs_end_bb = m_func_builder->NewBasicBlock();
  ExpressionBuilder eb_rhs(m_func_builder);
  if (!node->GetLHSExpression()->Accept(&eb_rhs) || !eb_rhs.IsValid())
    return false;

  llvm::BasicBlock* rhs_end_bb = m_func_builder->NewBasicBlock();
  llvm::BasicBlock* merge_bb = m_func_builder->GetCurrentBasicBlock();

  // Logical AND
  if (node->GetOperator() == AST::LogicalExpression::And)
  {
    // Branch to right-hand side if the left-hand side is true, otherwise branch to end
    llvm::IRBuilder<>(lhs_end_bb).CreateCondBr(eb_lhs.GetResultValue(), rhs_end_bb, merge_bb);

    // Branch to merge point after evaluating the right-hand side
    llvm::IRBuilder<>(rhs_end_bb).CreateBr(merge_bb);

    // Create phi node from result
    llvm::PHINode* phi = GetIRBuilder().CreatePHI(GetContext()->GetLLVMType(Type::GetBooleanType()), 2, "");
    phi->addIncoming(GetIRBuilder().getInt1(0), lhs_end_bb);
    phi->addIncoming(eb_rhs.GetResultValue(), rhs_end_bb);
    m_result_value = phi;
  }

  // Logical OR
  else if (node->GetOperator() == AST::LogicalExpression::Or)
  {
    // Branch to merge point if left-hand side is true, otherwise branch to right-hand side
    llvm::IRBuilder<>(lhs_end_bb).CreateCondBr(eb_lhs.GetResultValue(), merge_bb, rhs_end_bb);

    // Branch to merge point after evaluating the right-hand side
    llvm::IRBuilder<>(rhs_end_bb).CreateBr(merge_bb);

    // Create phi node from result
    llvm::PHINode* phi = GetIRBuilder().CreatePHI(GetContext()->GetLLVMType(Type::GetBooleanType()), 2, "");
    phi->addIncoming(GetIRBuilder().getInt1(1), lhs_end_bb);
    phi->addIncoming(eb_rhs.GetResultValue(), rhs_end_bb);
    m_result_value = phi;
  }

  return IsValid();
}
}