#include "parser/ast.h"
#include "parser/ast_visitor.h"

namespace AST
{

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

bool AssignmentExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool IntegerLiteralExpression::Accept(Visitor* visitor)
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