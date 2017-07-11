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
  if (!li)
    return;

  // Automatically merge child nodes
  NodeList* node_list = dynamic_cast<NodeList*>(li);
  if (node_list)
  {
    for (Node* node : node_list->m_nodes)
      AddNode(node);

    return;
  }

  m_nodes.push_back(li);
}

Declaration::Declaration(const SourceLocation& sloc) : m_sloc(sloc)
{
}

const SourceLocation& Declaration::GetSourceLocation() const
{
  return m_sloc;
}

Statement::Statement(const SourceLocation& sloc) : m_sloc(sloc)
{
}

const SourceLocation& Statement::GetSourceLocation() const
{
  return m_sloc;
}

Expression::Expression(const SourceLocation& sloc) : m_sloc(sloc), m_type(Type::GetErrorType())
{
}

const SourceLocation& Expression::GetSourceLocation() const
{
  return m_sloc;
}

bool Expression::IsConstant() const
{
  return false;
}

const Type* Expression::GetType() const
{
  return m_type;
}

PipelineDeclaration::PipelineDeclaration(const SourceLocation& sloc, const Type* input_type, const Type* output_type,
                                         const char* name, NodeList* statements)
  : Declaration(sloc), m_input_type(input_type), m_output_type(output_type), m_name(name), m_statements(statements)
{
}

PipelineDeclaration::~PipelineDeclaration()
{
}

bool PipelineDeclaration::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

PipelineAddStatement::PipelineAddStatement(const SourceLocation& sloc, const char* filter_name,
                                           const NodeList* parameters)
  : Statement(sloc), m_filter_name(filter_name), m_filter_parameters(parameters)
{
}

PipelineAddStatement::~PipelineAddStatement()
{
}

bool PipelineAddStatement::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

IdentifierExpression::IdentifierExpression(const SourceLocation& sloc, const char* identifier)
  : Expression(sloc), m_identifier(identifier)
{
  m_type = Type::GetErrorType();
}

VariableDeclaration* IdentifierExpression::GetReferencedVariable() const
{
  return m_identifier_declaration;
}

BinaryExpression::BinaryExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs), m_op(op)
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

RelationalExpression::RelationalExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs), m_intermediate_type(Type::GetErrorType()), m_op(op)
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

LogicalExpression::LogicalExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs), m_op(op)
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

CommaExpression::CommaExpression(const SourceLocation& sloc, Expression* lhs, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs)
{
}

Expression* CommaExpression::GetLHSExpression() const
{
  return m_lhs;
}

Expression* CommaExpression::GetRHSExpression() const
{
  return m_rhs;
}

AssignmentExpression::AssignmentExpression(const SourceLocation& sloc, Expression* lhs, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs)
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

IntegerLiteralExpression::IntegerLiteralExpression(const SourceLocation& sloc, int value)
  : Expression(sloc), m_value(value)
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

BooleanLiteralExpression::BooleanLiteralExpression(const SourceLocation& sloc, bool value)
  : Expression(sloc), m_value(value)
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

PeekExpression::PeekExpression(const SourceLocation& sloc, Expression* expr) : Expression(sloc), m_expr(expr)
{
}

PopExpression::PopExpression(const SourceLocation& sloc) : Expression(sloc)
{
}

PushExpression::PushExpression(const SourceLocation& sloc, Expression* expr) : Expression(sloc), m_expr(expr)
{
}

VariableDeclaration::VariableDeclaration(const SourceLocation& sloc, const Type* type, const char* name,
                                         Expression* initializer)
  : Declaration(sloc), m_type(type), m_name(name), m_initializer(initializer)
{
  // TODO: Default initialize ints to 0?
  // if (!m_initializer)
}

Node* VariableDeclaration::CreateDeclarations(const Type* type, const InitDeclaratorList* declarator_list)
{
  // Optimization for single declaration case
  if (declarator_list->size() == 1)
    return new VariableDeclaration(declarator_list->front().sloc, type, declarator_list->front().name,
                                   declarator_list->front().initializer);

  NodeList* decl_list = new NodeList();
  for (const InitDeclarator& decl : *declarator_list)
    decl_list->AddNode(new VariableDeclaration(decl.sloc, type, decl.name, decl.initializer));
  return decl_list;
}

FilterDeclaration::FilterDeclaration(const SourceLocation& sloc, const Type* input_type, const Type* output_type,
                                     const char* name, NodeList* vars, FilterWorkBlock* init, FilterWorkBlock* prework,
                                     FilterWorkBlock* work)
  : Declaration(sloc), m_input_type(input_type), m_output_type(output_type), m_name(name), m_vars(vars), m_init(init),
    m_prework(prework), m_work(work)
{
}

ExpressionStatement::ExpressionStatement(const SourceLocation& sloc, Expression* expr) : Statement(sloc), m_expr(expr)
{
}

Expression* ExpressionStatement::GetInnerExpression() const
{
  return m_expr;
}

IfStatement::IfStatement(const SourceLocation& sloc, Expression* expr, Node* then_stmts, Node* else_stmts)
  : Statement(sloc), m_expr(expr), m_then(then_stmts), m_else(else_stmts)
{
}

Expression* IfStatement::GetInnerExpression() const
{
  return m_expr;
}

Node* IfStatement::GetThenStatements() const
{
  return m_then;
}

Node* IfStatement::GetElseStatements() const
{
  return m_else;
}

bool IfStatement::HasElseStatements() const
{
  return (m_else != nullptr);
}

ForStatement::ForStatement(const SourceLocation& sloc, Node* init, Expression* cond, Expression* loop, Node* inner)
  : Statement(sloc), m_init(init), m_cond(cond), m_loop(loop), m_inner(inner)
{
}

Node* ForStatement::GetInitStatements() const
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

Node* ForStatement::GetInnerStatements() const
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

BreakStatement::BreakStatement(const SourceLocation& sloc) : Statement(sloc)
{
}

ContinueStatement::ContinueStatement(const SourceLocation& sloc) : Statement(sloc)
{
}

ReturnStatement::ReturnStatement(const SourceLocation& sloc, Expression* expr) : Statement(sloc), m_expr(expr)
{
}

Expression* ReturnStatement::GetInnerExpression() const
{
  return m_expr;
}

bool ReturnStatement::HasReturnValue() const
{
  return (m_expr != nullptr);
}
}
