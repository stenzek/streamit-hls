#include "frontend/statement_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/expression_builder.h"
#include "frontend/filter_function_builder.h"
#include "llvm/IR/Constants.h"
#include "parser/ast.h"

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
}