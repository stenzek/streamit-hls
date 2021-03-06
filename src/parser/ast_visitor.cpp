#include "parser/ast_visitor.h"
#include "parser/ast.h"

namespace AST
{

bool Visitor::Visit(ExpressionStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(IfStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(ForStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(BreakStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(ContinueStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(ReturnStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(InitializerListExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(VariableDeclaration* node)
{
  return Visit(static_cast<Declaration*>(node));
}

bool Visitor::Visit(PushStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(PopExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(CallExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(CastExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(PeekExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(CommaExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(AssignmentExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(UnaryExpression* node)
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

bool Visitor::Visit(IndexExpression* node)
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

bool Visitor::Visit(FloatLiteralExpression* node)
{
  return Visit(static_cast<Expression*>(node));
}

bool Visitor::Visit(FilterWorkBlock* node)
{
  return true;
}

bool Visitor::Visit(FilterDeclaration* node)
{
  return Visit(static_cast<StreamDeclaration*>(node));
}

bool Visitor::Visit(FunctionDeclaration* node)
{
  return Visit(static_cast<Declaration*>(node));
}

bool Visitor::Visit(AddStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(SplitStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(JoinStatement* node)
{
  return Visit(static_cast<Statement*>(node));
}

bool Visitor::Visit(ParameterDeclaration* node)
{
  return Visit(static_cast<ParameterDeclaration*>(node));
}

bool Visitor::Visit(StreamDeclaration* node)
{
  return Visit(static_cast<Node*>(node));
}

bool Visitor::Visit(PipelineDeclaration* node)
{
  return Visit(static_cast<StreamDeclaration*>(node));
}

bool Visitor::Visit(SplitJoinDeclaration* node)
{
  return Visit(static_cast<StreamDeclaration*>(node));
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

bool Visitor::Visit(TypeSpecifier* node)
{
  return Visit(static_cast<Node*>(node));
}

bool Visitor::Visit(ArrayTypeSpecifier* node)
{
  return Visit(static_cast<TypeSpecifier*>(node));
}

bool Visitor::Visit(Node* node)
{
  return true;
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

bool TypeSpecifier::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool ArrayTypeSpecifier::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool IdentifierExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool IndexExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}
bool UnaryExpression::Accept(Visitor* visitor)
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

bool CommaExpression::Accept(Visitor* visitor)
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

bool FloatLiteralExpression::Accept(Visitor* visitor)
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

bool CallExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool CastExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool PushStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool InitializerListExpression::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool VariableDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool FilterDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool ExpressionStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool IfStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool FilterWorkBlock::Accept(Visitor* visitor)
{
  if (m_stmts)
    return m_stmts->Accept(visitor);
  else
    return true;
}

bool ForStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool BreakStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool ContinueStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool ReturnStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool PipelineDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool SplitJoinDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool FunctionDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool AddStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool JoinStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool SplitStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool ParameterDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

} // namespace AST