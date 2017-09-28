#include "parser/ast.h"
#include <cassert>
#include "parser/ast_visitor.h"

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

const AST::Node* NodeList::GetChild(size_t index) const
{
  size_t current_index = 0;
  for (const Node* node : m_nodes)
  {
    if (index == current_index)
      return node;

    current_index++;
  }

  return nullptr;
}

AST::Node* NodeList::GetChild(size_t index)
{
  size_t current_index = 0;
  for (Node* node : m_nodes)
  {
    if (index == current_index)
      return node;

    current_index++;
  }

  return nullptr;
}

TypeSpecifier::TypeSpecifier(TypeId tid, const std::string& name, TypeSpecifier* base_type, unsigned num_bits)
  : m_type_id(tid), m_name(name), m_base_type(base_type), m_num_bits(num_bits)
{
}

TypeSpecifier* TypeSpecifier::Clone() const
{
  return new TypeSpecifier(m_type_id, m_name, m_base_type, m_num_bits);
}

bool TypeSpecifier::operator==(const TypeSpecifier& rhs) const
{
  if ((m_base_type != nullptr) != (rhs.m_base_type != nullptr))
    return false;

  if (m_base_type)
    return *m_base_type == *rhs.m_base_type;

  return (m_type_id == rhs.m_type_id);
}

bool TypeSpecifier::operator!=(const TypeSpecifier& rhs) const
{
  return !operator==(rhs);
}

ArrayTypeSpecifier::ArrayTypeSpecifier(const std::string& name, TypeSpecifier* base_type, Expression* dimensions)
  : TypeSpecifier(TypeSpecifier::TypeId::Array, name, base_type, 0), m_array_dimensions(dimensions)
{
}

TypeSpecifier* ArrayTypeSpecifier::Clone() const
{
  return new ArrayTypeSpecifier(m_name, m_base_type->Clone(), m_array_dimensions);
}

bool ArrayTypeSpecifier::operator==(const TypeSpecifier& rhs) const
{
  if (!rhs.IsArrayType())
    return false;

  // The expressions may result in the same value.. so just return true for all arrays
  return (*m_base_type == *static_cast<const ArrayTypeSpecifier&>(rhs).m_base_type);
}

Declaration::Declaration(const SourceLocation& sloc, TypeSpecifier* type, const std::string& name, bool constant)
  : m_sloc(sloc), m_type(type), m_name(name), m_constant(constant)
{
}

Statement::Statement(const SourceLocation& sloc) : m_sloc(sloc)
{
}

Expression::Expression(const SourceLocation& sloc) : m_sloc(sloc), m_type(nullptr)
{
}

bool Expression::IsConstant() const
{
  return false;
}

bool Expression::GetConstantBool() const
{
  return false;
}

int Expression::GetConstantInt() const
{
  return 0;
}

float Expression::GetConstantFloat() const
{
  return 0.0f;
}

const TypeSpecifier* Expression::GetType() const
{
  return m_type;
}

ParameterDeclaration::ParameterDeclaration(const SourceLocation& sloc, TypeSpecifier* type_specifier,
                                           const std::string& name)
  : Declaration(sloc, type_specifier, name, true)
{
  m_type = type_specifier;
}

StreamDeclaration::StreamDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                                     TypeSpecifier* output_type_specifier, const char* name,
                                     ParameterDeclarationList* params)
  : m_sloc(sloc), m_input_type(input_type_specifier), m_output_type(output_type_specifier), m_name(name),
    m_parameters(params)
{
}

PipelineDeclaration::PipelineDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                                         TypeSpecifier* output_type_specifier, const char* name,
                                         ParameterDeclarationList* params, NodeList* statements)
  : StreamDeclaration(sloc, input_type_specifier, output_type_specifier, name, params), m_statements(statements)
{
}

PipelineDeclaration::~PipelineDeclaration()
{
}

SplitJoinDeclaration::SplitJoinDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                                           TypeSpecifier* output_type_specifier, const char* name,
                                           ParameterDeclarationList* params, NodeList* statements)
  : StreamDeclaration(sloc, input_type_specifier, output_type_specifier, name, params), m_statements(statements)
{
}

SplitJoinDeclaration::~SplitJoinDeclaration()
{
}

// FunctionDeclaration::FunctionDeclaration(const SourceLocation& sloc, const char* name, TypeSpecifier* return_type,
//                                          NodeList* params, NodeList* body)
//   : Declaration(sloc, name, true), m_return_type_specifier(return_type), m_params(params), m_body(body)
// {
// }

AddStatement::AddStatement(const SourceLocation& sloc, const char* filter_name, NodeList* type_parameters,
                           NodeList* parameters)
  : Statement(sloc), m_stream_name(filter_name), m_type_parameters(type_parameters), m_stream_parameters(parameters)
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

int BinaryExpression::GetConstantInt() const
{
  switch (m_op)
  {
  case Add:
    return m_lhs->GetConstantInt() + m_rhs->GetConstantInt();

  case Subtract:
    return m_lhs->GetConstantInt() - m_rhs->GetConstantInt();

  case Multiply:
    return m_lhs->GetConstantInt() * m_rhs->GetConstantInt();

  case Divide:
    if (m_rhs->GetConstantInt() == 0)
      return 0;
    return m_lhs->GetConstantInt() / m_rhs->GetConstantInt();

  case Modulo:
    if (m_rhs->GetConstantInt() == 0)
      return 0;
    return m_lhs->GetConstantInt() % m_rhs->GetConstantInt();

  case BitwiseAnd:
    return m_lhs->GetConstantInt() & m_rhs->GetConstantInt();

  case BitwiseOr:
    return m_lhs->GetConstantInt() | m_rhs->GetConstantInt();

  case BitwiseXor:
    return m_lhs->GetConstantInt() ^ m_rhs->GetConstantInt();

  case LeftShift:
    return m_lhs->GetConstantInt() << m_rhs->GetConstantInt();

  case RightShift:
    return m_lhs->GetConstantInt() >> m_rhs->GetConstantInt();

  default:
    return 0;
  }
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

const TypeSpecifier* RelationalExpression::GetIntermediateType() const
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

AssignmentExpression::AssignmentExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs)
  : Expression(sloc), m_lhs(lhs), m_rhs(rhs), m_op(op)
{
  // Depending on the operator, create a node so that x += y turns into x = x + y.
  switch (m_op)
  {
  case Add:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::Add, rhs);
    break;

  case Subtract:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::Subtract, rhs);
    break;

  case Multiply:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::Multiply, rhs);
    break;

  case Divide:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::Divide, rhs);
    break;

  case Modulo:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::Modulo, rhs);
    break;

  case BitwiseAnd:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::BitwiseAnd, rhs);
    break;

  case BitwiseOr:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::BitwiseOr, rhs);
    break;

  case BitwiseXor:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::BitwiseXor, rhs);
    break;

  case LeftShift:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::LeftShift, rhs);
    break;

  case RightShift:
    m_rhs = new BinaryExpression(sloc, lhs, BinaryExpression::RightShift, rhs);
    break;

  case Assign:
  default:
    break;
  }
}

IntegerLiteralExpression::IntegerLiteralExpression(const SourceLocation& sloc, int value)
  : Expression(sloc), m_value(value)
{
}

BooleanLiteralExpression::BooleanLiteralExpression(const SourceLocation& sloc, bool value)
  : Expression(sloc), m_value(value)
{
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

CastExpression::CastExpression(const SourceLocation& sloc, TypeSpecifier* to_type, Expression* expr)
  : Expression(sloc), m_to_type(to_type), m_expr(expr)
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

VariableDeclaration::VariableDeclaration(const SourceLocation& sloc, TypeSpecifier* type_specifier, const char* name,
                                         Expression* initializer)
  : Declaration(sloc, type_specifier, name, false), m_initializer(initializer)
{
  // TODO: Default initialize ints to 0?
  // if (!m_initializer)
}

Node* VariableDeclaration::CreateDeclarations(TypeSpecifier* type_specifier, const InitDeclaratorList* declarator_list)
{
  // Optimization for single declaration case
  if (declarator_list->size() == 1)
    return new VariableDeclaration(declarator_list->front().sloc, type_specifier, declarator_list->front().name,
                                   declarator_list->front().initializer);

  NodeList* decl_list = new NodeList();
  for (const InitDeclarator& decl : *declarator_list)
    decl_list->AddNode(new VariableDeclaration(decl.sloc, type_specifier, decl.name, decl.initializer));
  return decl_list;
}

FilterDeclaration::FilterDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                                     TypeSpecifier* output_type_specifier, const char* name,
                                     ParameterDeclarationList* params, NodeList* vars, FilterWorkBlock* init,
                                     FilterWorkBlock* prework, FilterWorkBlock* work, bool stateful)
  : StreamDeclaration(sloc, input_type_specifier, output_type_specifier, name, params), m_vars(vars), m_init(init),
    m_prework(prework), m_work(work), m_stateful(stateful), m_builtin(false)
{
}

FilterDeclaration::FilterDeclaration(TypeSpecifier* input_type_specifier, TypeSpecifier* output_type_specifier,
                                     const char* name, ParameterDeclarationList* params, bool stateful, int peek_rate,
                                     int pop_rate, int push_rate)
  : StreamDeclaration({}, input_type_specifier, output_type_specifier, name, params), m_vars(nullptr), m_init(nullptr),
    m_prework(nullptr), m_work(nullptr), m_stateful(stateful), m_builtin(true)
{
  m_work = new FilterWorkBlock({});
  m_work->SetPeekRateExpression(new IntegerLiteralExpression({}, peek_rate));
  m_work->SetPopRateExpression(new IntegerLiteralExpression({}, pop_rate));
  m_work->SetPushRateExpression(new IntegerLiteralExpression({}, push_rate));
  m_work->SetStatements(new NodeList());
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

SplitStatement::SplitStatement(const SourceLocation& sloc, Type type, NodeList* distribution)
  : Statement(sloc), m_type(type), m_distribution(distribution)
{
}

JoinStatement::JoinStatement(const SourceLocation& sloc, Type type, NodeList* distribution)
  : Statement(sloc), m_type(type), m_distribution(distribution)
{
}
}
