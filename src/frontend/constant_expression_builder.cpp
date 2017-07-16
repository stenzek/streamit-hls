#include "frontend/constant_expression_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/filter_function_builder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "parser/ast.h"
#include "parser/type.h"

namespace Frontend
{
ConstantExpressionBuilder::ConstantExpressionBuilder(Context* context) : m_context(context)
{
}

ConstantExpressionBuilder::~ConstantExpressionBuilder()
{
}

bool ConstantExpressionBuilder::IsValid() const
{
  return (m_result_value != nullptr);
}

llvm::Constant* ConstantExpressionBuilder::GetResultValue()
{
  return m_result_value;
}

bool ConstantExpressionBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback handler executed");
  return false;
}

bool ConstantExpressionBuilder::Visit(AST::IntegerLiteralExpression* node)
{
  llvm::Type* llvm_type = m_context->GetLLVMType(node->GetType());
  assert(llvm_type);

  m_result_value = llvm::ConstantInt::get(llvm_type, static_cast<uint64_t>(node->GetValue()), true);
  return IsValid();
}

bool ConstantExpressionBuilder::Visit(AST::BooleanLiteralExpression* node)
{
  llvm::Type* llvm_type = m_context->GetLLVMType(node->GetType());
  assert(llvm_type);

  m_result_value = llvm::ConstantInt::get(llvm_type, node->GetValue() ? 1 : 0);
  return IsValid();
}

bool ConstantExpressionBuilder::Visit(AST::InitializerListExpression* node)
{
  llvm::Type* llvm_type = m_context->GetLLVMType(node->GetType());
  llvm::ArrayType* llvm_array_type = llvm::cast<llvm::ArrayType>(llvm_type);
  assert(llvm_type && llvm_array_type);

  llvm::SmallVector<llvm::Constant*, 16> llvm_values;
  for (AST::Expression* expr : node->GetExpressionList())
  {
    ConstantExpressionBuilder element_ceb(m_context);
    if (!expr->Accept(&element_ceb) || !element_ceb.IsValid())
      return false;

    llvm_values.push_back(element_ceb.GetResultValue());
  }

  m_result_value = llvm::ConstantArray::get(llvm_array_type, llvm_values);
  return IsValid();
}
}