#include "ast.h"
#include <cassert>
#include "type.h"

namespace AST
{
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

PipelineAddStatement::PipelineAddStatement(const char* filter_name, const NodeList* parameters)
  : m_filter_name(filter_name), m_filter_parameters(parameters)
{
}

PipelineAddStatement::~PipelineAddStatement()
{
}

IdentifierExpression::IdentifierExpression(const char* identifier) : m_identifier(identifier)
{
  m_type = Type::GetErrorType();
}

BinaryExpression::BinaryExpression(Expression* lhs, Operator op, Expression* rhs) : m_lhs(lhs), m_rhs(rhs), m_op(op)
{
  m_type = Type::GetErrorType();
}

AssignmentExpression::AssignmentExpression(const char* identifier, Expression* rhs)
  : m_identifier(identifier), m_rhs(rhs)
{
  m_type = Type::GetErrorType();
}

IntegerLiteralExpression::IntegerLiteralExpression(int value) : m_value(value)
{
  m_type = Type::GetIntegerType();
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
}
