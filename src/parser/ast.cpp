#include "parser/ast.h"
#include <cassert>
#include "parser/ast_visitor.h"
#include "parser/type.h"

namespace AST
{
bool NodeList::HasChildren() const
{
  return !m_nodes.empty();
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

void NodeList::PrependNode(Node* node)
{
  if (!node)
    return;

  NodeList* node_list = dynamic_cast<NodeList*>(node);
  if (node_list)
  {
    if (!node_list->m_nodes.empty())
      m_nodes.insert(m_nodes.begin(), node_list->m_nodes.begin(), node_list->m_nodes.end());

    return;
  }

  m_nodes.insert(m_nodes.begin(), node);
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

Expression::Expression(const SourceLocation& sloc) : m_sloc(sloc), m_type(nullptr)
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

TypeReference::TypeReference(const std::string& name, const Type* type) : m_name(name), m_type(type)
{
}

TypeName::TypeName(const SourceLocation& sloc) : m_sloc(sloc)
{
}

TypeName::TypeName(const Type* from_type) : m_sloc{}
{
  if (from_type->IsPrimitiveType())
  {
    m_base_type_name = from_type->GetName();
    return;
  }

  if (from_type->IsArrayType())
  {
    m_base_type_name = from_type->GetBaseType()->GetName();
    m_array_sizes = static_cast<const ArrayType*>(from_type)->GetArraySizes();
  }
}

const std::string& TypeName::GetBaseTypeName() const
{
  return m_base_type_name;
}

const std::vector<int>& TypeName::GetArraySizes() const
{
  return m_array_sizes;
}

const Type* TypeName::GetFinalType() const
{
  return m_final_type;
}

void TypeName::SetBaseTypeName(const char* name)
{
  m_base_type_name = name;
}

void TypeName::AddArraySize(int size)
{
  m_array_sizes.push_back(size);
}

void TypeName::Merge(ParserState* state, TypeName* rhs)
{
  if (m_base_type_name.empty() && !rhs->m_base_type_name.empty())
    m_base_type_name = rhs->m_base_type_name;

  if (!rhs->m_array_sizes.empty())
    m_array_sizes.insert(m_array_sizes.end(), rhs->m_array_sizes.begin(), rhs->m_array_sizes.end());
}

StructSpecifier::StructSpecifier(const SourceLocation& sloc, const char* name) : m_sloc(sloc), m_name(name)
{
}

const std::string& StructSpecifier::GetName() const
{
  return m_name;
}

const std::vector<std::pair<std::string, TypeName*>>& StructSpecifier::GetFields() const
{
  return m_fields;
}

void StructSpecifier::AddField(const char* name, TypeName* specifier)
{
  m_fields.emplace_back(name, specifier);
}

StreamDeclaration::StreamDeclaration(const SourceLocation& sloc, const char* name) : Declaration(sloc), m_name(name)
{
}

PipelineDeclaration::PipelineDeclaration(const SourceLocation& sloc, TypeName* input_type_specifier,
                                         TypeName* output_type_specifier, const char* name, NodeList* statements)
  : StreamDeclaration(sloc, name), m_input_type_specifier(input_type_specifier),
    m_output_type_specifier(output_type_specifier), m_statements(statements)
{
}

PipelineDeclaration::~PipelineDeclaration()
{
}

SplitJoinDeclaration::SplitJoinDeclaration(const SourceLocation& sloc, TypeName* input_type_specifier,
                                           TypeName* output_type_specifier, const char* name, NodeList* statements)
  : StreamDeclaration(sloc, name), m_input_type_specifier(input_type_specifier),
    m_output_type_specifier(output_type_specifier), m_statements(statements)
{
}

SplitJoinDeclaration::~SplitJoinDeclaration()
{
}

FunctionDeclaration::FunctionDeclaration(const SourceLocation& sloc, const char* name, TypeName* return_type,
                                         NodeList* params, NodeList* body)
  : Declaration(sloc), m_name(name), m_return_type_specifier(return_type), m_params(params), m_body(body)
{
}

AddStatement::AddStatement(const SourceLocation& sloc, const char* filter_name, const NodeList* parameters)
  : Statement(sloc), m_stream_name(filter_name), m_stream_parameters(parameters)
{
}

AddStatement::~AddStatement()
{
}

IdentifierExpression::IdentifierExpression(const SourceLocation& sloc, const char* identifier)
  : Expression(sloc), m_identifier(identifier)
{
}

IndexExpression::IndexExpression(const SourceLocation& sloc, Expression* array_expr, Expression* index_expr)
  : Expression(sloc), m_array_expression(array_expr), m_index_expression(index_expr)
{
}

Expression* IndexExpression::GetArrayExpression() const
{
  return m_array_expression;
}

Expression* IndexExpression::GetIndexExpression() const
{
  return m_index_expression;
}

UnaryExpression::UnaryExpression(const SourceLocation& sloc, Operator op, Expression* rhs)
  : Expression(sloc), m_op(op), m_rhs(rhs)
{
}

bool UnaryExpression::IsConstant() const
{
  // This is needed to parse x = -5.
  return (m_op >= Positive && m_op <= Negative && m_rhs->IsConstant());
}

UnaryExpression::Operator UnaryExpression::GetOperator() const
{
  return m_op;
}

Expression* UnaryExpression::GetRHSExpression() const
{
  return m_rhs;
}

BinaryExpression::BinaryExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs), m_op(op)
{
}

bool BinaryExpression::IsConstant() const
{
  return (m_lhs->IsConstant() && m_rhs->IsConstant());
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
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs), m_intermediate_type(nullptr), m_op(op)
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

Expression* AssignmentExpression::GetLValueExpression() const
{
  return m_lhs;
}

Expression* AssignmentExpression::GetInnerExpression() const
{
  return m_rhs;
}

IntegerLiteralExpression::IntegerLiteralExpression(const SourceLocation& sloc, int value)
  : Expression(sloc), m_value(value)
{
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

Expression* PeekExpression::GetIndexExpression() const
{
  return m_expr;
}

PopExpression::PopExpression(const SourceLocation& sloc) : Expression(sloc)
{
}

CallExpression::CallExpression(const SourceLocation& sloc, const char* function_name, NodeList* args)
  : Expression(sloc), m_function_name(function_name), m_args(args)
{
}

PushStatement::PushStatement(const SourceLocation& sloc, Expression* expr) : Statement(sloc), m_expr(expr)
{
}

Expression* PushStatement::GetValueExpression() const
{
  return m_expr;
}

InitializerListExpression::InitializerListExpression(const SourceLocation& sloc) : Expression(sloc)
{
}

bool InitializerListExpression::IsConstant() const
{
  // The initializer list is constant if everything in it is constant
  for (Expression* expr : m_expressions)
  {
    if (!expr->IsConstant())
      return false;
  }

  return true;
}

void InitializerListExpression::AddExpression(Expression* expr)
{
  m_expressions.push_back(expr);
}

const std::vector<Expression*>& InitializerListExpression::GetExpressionList() const
{
  return m_expressions;
}

size_t InitializerListExpression::GetListSize() const
{
  return m_expressions.size();
}

VariableDeclaration::VariableDeclaration(const SourceLocation& sloc, TypeName* type_specifier, const char* name,
                                         Expression* initializer)
  : Declaration(sloc), m_type_specifier(type_specifier), m_name(name), m_initializer(initializer)
{
  // TODO: Default initialize ints to 0?
  // if (!m_initializer)
}

Node* VariableDeclaration::CreateDeclarations(TypeName* type_specifier, const InitDeclaratorList* declarator_list)
{
  // Optimization for single declaration case
  if (declarator_list->size() == 1)
    return new VariableDeclaration(declarator_list->front().sloc, type_specifier, declarator_list->front().name,
                                   declarator_list->front().initializer);

  // We need to clone the type specifier for each declaration, otherwise we'll call SemanticAnalysis etc multiple times
  // on the same specifier
  NodeList* decl_list = new NodeList();
  for (const InitDeclarator& decl : *declarator_list)
    decl_list->AddNode(new VariableDeclaration(decl.sloc, new TypeName(*type_specifier), decl.name, decl.initializer));
  return decl_list;
}

FilterDeclaration::FilterDeclaration(const SourceLocation& sloc, TypeName* input_type_specifier,
                                     TypeName* output_type_specifier, const char* name, NodeList* vars,
                                     FilterWorkBlock* init, FilterWorkBlock* prework, FilterWorkBlock* work)
  : StreamDeclaration(sloc, name), m_input_type_specifier(input_type_specifier),
    m_output_type_specifier(output_type_specifier), m_vars(vars), m_init(init), m_prework(prework), m_work(work)
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

SplitStatement::SplitStatement(const SourceLocation& sloc, Type type) : Statement(sloc), m_type(type)
{
}

SplitStatement::Type SplitStatement::GetType() const
{
  return m_type;
}

JoinStatement::JoinStatement(const SourceLocation& sloc, Type type) : Statement(sloc), m_type(type)
{
}

JoinStatement::Type JoinStatement::GetType() const
{
  return m_type;
}
}
