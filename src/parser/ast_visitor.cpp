#include "parser/ast.h"
#include "parser/ast_visitor.h"

namespace AST
{

bool Visitor::Visit(ExpressionStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(VariableDeclaration* node)
{
  return Visit(static_cast<Declaration*>(node));
}

bool Visitor::Visit(PushExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(PopExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(PeekExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(AssignmentExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(BinaryExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(RelationalExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(LogicalExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(IdentifierExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(BooleanLiteralExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(IntegerLiteralExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(FilterWorkBlock* node)
{
  return true;
}

bool Visitor::Visit(FilterDeclaration* node)
{
  return Visit(static_cast<Declaration*>(node));
}

bool Visitor::Visit(PipelineAddStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(PipelineDeclaration* node)
{
  return Visit(static_cast<Declaration*>(node));
}

bool Visitor::Visit(Expression* node)
{
  return Visit(static_cast<Node*>(node));
}

bool Visitor::Visit(Declaration* node)
{
  return Visit(static_cast<Node*>(node));
}

bool Visitor::Visit(Statement* node)
{
  return Visit(static_cast<Node*>(node));
}

bool Visitor::Visit(Node* node)
{
  return true;
}

bool Visitor::Visit(Program* node)
{
  return true;
}

bool Program::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool NodeList::Accept(Visitor* visitor)
{
  for (Node* node : m_nodes)
  {
    if (!node->Accept(visitor))
      return false;
  }

  return true;
}

bool IdentifierExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool BinaryExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool RelationalExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool LogicalExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool AssignmentExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool IntegerLiteralExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool BooleanLiteralExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool PeekExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}
bool PopExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool PushExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool VariableDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool FilterDeclaration::Accept(Visitor* visitor)
{
  visitor->Visit(this);
}

bool ExpressionStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool FilterWorkBlock::Accept(Visitor* visitor)
{
  if (m_stmts)
    return m_stmts->Accept(visitor);
}

} // namespace AST