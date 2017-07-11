#include "frontend/statement_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/expression_builder.h"
#include "frontend/filter_function_builder.h"
#include "llvm/IR/Constants.h"
#include "parser/ast.h"
#include "parser/type.h"

namespace Frontend
{
StatementBuilder::StatementBuilder(FilterFunctionBuilder* bb_builder) : m_func_builder(bb_builder)
{
}

StatementBuilder::~StatementBuilder()
{
}

Context* StatementBuilder::GetContext() const
{
  return m_func_builder->GetContext();
}

llvm::IRBuilder<>& StatementBuilder::GetIRBuilder() const
{
  return m_func_builder->GetCurrentIRBuilder();
}

bool StatementBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback handler executed");
  return false;
}

bool StatementBuilder::Visit(AST::ExpressionStatement* node)
{
  // No need to assign the result to anywhere.
  ExpressionBuilder eb(m_func_builder);
  if (!node->GetInnerExpression()->Accept(&eb) || !eb.IsValid())
    return false;

  return true;
}

bool StatementBuilder::Visit(AST::IfStatement* node)
{
  ExpressionBuilder eb(m_func_builder);
  if (!node->GetInnerExpression()->Accept(&eb) || !eb.IsValid())
    return false;

  // Expression needs to be boolean
  if (node->GetInnerExpression()->GetType() != Type::GetBooleanType())
    return false;

  // Create a basic block for the then part
  auto* before_branch_bb = m_func_builder->NewBasicBlock();

  // Evaluate then statements into this new block
  if (!node->GetThenStatements()->Accept(m_func_builder))
    return false;

  // Create a new basic block, either for the else part, or the next statements
  auto* then_bb = m_func_builder->NewBasicBlock();
  auto* else_bb = m_func_builder->GetCurrentBasicBlock();
  auto* merge_bb = m_func_builder->GetCurrentBasicBlock();

  // If there is an else part, place them in this block
  if (node->HasElseStatements())
  {
    if (!node->GetElseStatements()->Accept(m_func_builder))
      return false;

    // Jump to the merge bb after the else
    else_bb = m_func_builder->NewBasicBlock();
    merge_bb = m_func_builder->GetCurrentBasicBlock();
    llvm::IRBuilder<>(else_bb).CreateBr(merge_bb);
  }

  // Jump to the then bb, otherwise the else bb
  llvm::IRBuilder<>(before_branch_bb).CreateCondBr(eb.GetResultValue(), then_bb, else_bb);

  // Jump to the merge bb at the end of the then bb
  llvm::IRBuilder<>(then_bb).CreateBr(merge_bb);
  return true;
}
}