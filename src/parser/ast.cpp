#include "parser/ast.h"
#include <cassert>
#include "parser/ast_visitor.h"
#include "parser/type.h"

namespace AST
{

const std::list<FilterDeclaration*>& Program::GetFilterList() const
{
  return m_filters;
}

void Program::AddPipeline(PipelineDeclaration* decl)
{
  m_pipelines.push_back(decl);
}

void Program::AddFilter(FilterDeclaration* decl)
{
  m_filters.push_back(decl);
}

const AST::Node* NodeList::GetFirst() const
{
  assert(!m_nodes.empty());
  return m_nodes.front();
}

AST::Node* NodeList::GetFirst()
{
  assert(!m_nodes.empty());
  return m_nodes.front();
}

void NodeList::AddNode(Node* li)
{
  m_nodes.push_back(li);
}

void NodeList::AddNodes(NodeList* node_list)
{
  if (node_list)
  {
    for (Node* node : node_list->m_nodes)
      m_nodes.push_back(node);
  }
}

PipelineDeclaration::PipelineDeclaration(const Type* input_type, const Type* output_type, const char* name,
                                         NodeList* statements)
  : m_input_type(input_type), m_output_type(output_type), m_name(name), m_statements(statements)
{
}

PipelineDeclaration::~PipelineDeclaration()
{
}

bool PipelineDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

PipelineAddStatement::PipelineAddStatement(const char* filter_name, const NodeList* parameters)
  : m_filter_name(filter_name), m_filter_parameters(parameters)
{
}

PipelineAddStatement::~PipelineAddStatement()
{
}

bool PipelineAddStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

IdentifierExpression::IdentifierExpression(const char* identifier) : m_identifier(identifier)
{
  m_type = Type::GetErrorType();
}

VariableDeclaration* IdentifierExpression::GetReferencedVariable() const
{
  return m_identifier_declaration;
}

Expression::Expression() : m_type(Type::GetErrorType())
{
}

bool Expression::IsConstant() const
{
  return false;
}

const Type* Expression::GetType() const
{
  return m_type;
}

BinaryExpression::BinaryExpression(Expression* lhs, Operator op, Expression* rhs) : m_lhs(lhs), m_rhs(rhs), m_op(op)
{
}

Expression* BinaryExpression::GetLHSExpression() const
{
  return m_lhs;
}

Expression* BinaryExpression::GetRHSExpression() const
{
  return m_rhs;
}

BinaryExpression::Operator BinaryExpression::GetOperator() const
{
  return m_op;
}

RelationalExpression::RelationalExpression(Expression* lhs, Operator op, Expression* rhs)
  : m_lhs(lhs), m_rhs(rhs), m_intermediate_type(Type::GetErrorType()), m_op(op)
{
}

Expression* RelationalExpression::GetLHSExpression() const
{
  return m_lhs;
}

Expression* RelationalExpression::GetRHSExpression() const
{
  return m_rhs;
}

const Type* RelationalExpression::GetIntermediateType() const
{
  return m_intermediate_type;
}

RelationalExpression::Operator RelationalExpression::GetOperator() const
{
  return m_op;
}

LogicalExpression::LogicalExpression(Expression* lhs, Operator op, Expression* rhs) : m_lhs(lhs), m_rhs(rhs), m_op(op)
{
}

Expression* LogicalExpression::GetLHSExpression() const
{
  return m_lhs;
}

Expression* LogicalExpression::GetRHSExpression() const
{
  return m_rhs;
}

LogicalExpression::Operator LogicalExpression::GetOperator() const
{
  return m_op;
}

AssignmentExpression::AssignmentExpression(Expression* lhs, Expression* rhs) : m_lhs(lhs), m_rhs(rhs)
{
}

VariableDeclaration* AssignmentExpression::GetReferencedVariable() const
{
  return m_identifier_declaration;
}

Expression* AssignmentExpression::GetInnerExpression() const
{
  return m_rhs;
}

IntegerLiteralExpression::IntegerLiteralExpression(int value) : m_value(value)
{
  m_type = Type::GetIntType();
}

int IntegerLiteralExpression::GetValue() const
{
  return m_value;
}

bool IntegerLiteralExpression::IsConstant() const
{
  return true;
}

BooleanLiteralExpression::BooleanLiteralExpression(bool value) : m_value(value)
{
  m_type = Type::GetBooleanType();
}

bool BooleanLiteralExpression::GetValue() const
{
  return m_value;
}

bool BooleanLiteralExpression::IsConstant() const
{
  return true;
}

PeekExpression::PeekExpression(Expression* expr) : m_expr(expr)
{
}

PopExpression::PopExpression()
{
}

PushExpression::PushExpression(Expression* expr) : m_expr(expr)
{
}

VariableDeclaration::VariableDeclaration(const Type* type, const char* name, Expression* initializer)
  : m_type(type), m_name(name), m_initializer(initializer)
{
  // TODO: Default initialize ints to 0?
  // if (!m_initializer)
}

FilterDeclaration::FilterDeclaration(const Type* input_type, const Type* output_type, const char* name, NodeList* vars,
                                     FilterWorkBlock* init, FilterWorkBlock* prework, FilterWorkBlock* work)
  : m_input_type(input_type), m_output_type(output_type), m_name(name), m_vars(vars), m_init(init), m_prework(prework),
    m_work(work)
{
}

ExpressionStatement::ExpressionStatement(Expression* expr) : m_expr(expr)
{
}

Expression* ExpressionStatement::GetInnerExpression() const
{
  return m_expr;
}

IfStatement::IfStatement(Expression* expr, NodeList* then_stmts, NodeList* else_stmts)
  : m_expr(expr), m_then(then_stmts), m_else(else_stmts)
{
}

Expression* IfStatement::GetInnerExpression() const
{
  return m_expr;
}

NodeList* IfStatement::GetThenStatements() const
{
  return m_then;
}

NodeList* IfStatement::GetElseStatements() const
{
  return m_else;
}

bool IfStatement::HasElseStatements() const
{
  return (m_else != nullptr);
}

ForStatement::ForStatement(NodeList* init, Expression* cond, Expression* loop, NodeList* inner)
  : m_init(init), m_cond(cond), m_loop(loop), m_inner(inner)
{
}

NodeList* ForStatement::GetInitStatements() const
{
  return m_init;
}

Expression* ForStatement::GetConditionExpression() const
{
  return m_cond;
}

Expression* ForStatement::GetLoopExpression() const
{
  return m_loop;
}

NodeList* ForStatement::GetInnerStatements() const
{
  return m_inner;
}

bool ForStatement::HasInitStatements() const
{
  return (m_init != nullptr);
}

bool ForStatement::HasConditionExpression() const
{
  return (m_cond != nullptr);
}

bool ForStatement::HasLoopExpression() const
{
  return (m_loop != nullptr);
}

bool ForStatement::HasInnerStatements() const
{
  return (m_inner != nullptr);
}
}
