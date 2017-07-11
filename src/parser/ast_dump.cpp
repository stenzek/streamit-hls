#include "parser/ast.h"
#include <array>
#include <cassert>
#include "parser/ast_printer.h"
#include "parser/type.h"

namespace AST
{
void Program::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("Program");

  printer->BeginBlock("Pipelines");
  {
    unsigned int counter = 0;
    for (auto* pipeline : m_pipelines)
    {
      printer->Write("Pipeline[%u]: ", counter++);
      pipeline->Dump(printer);
    }
  }
  printer->EndBlock();

  printer->BeginBlock("Filters");
  {
    unsigned int counter = 0;
    for (auto* filter : m_filters)
    {
      printer->Write("Filter[%u]: ", counter++);
      filter->Dump(printer);
    }
  }
  printer->EndBlock();

  printer->EndBlock();
}

void NodeList::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("NodeList");

  unsigned int counter = 0;
  for (Node* node : m_nodes)
  {
    printer->Write("Node[%u]: ", counter++);
    node->Dump(printer);
  }

  printer->EndBlock();
}

void PipelineDeclaration::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("PipelineDeclaration[name=%s, input=%s, output=%s]", m_name.c_str(),
                      m_input_type->GetName().c_str(), m_output_type->GetName().c_str());
  if (m_statements)
  {
    unsigned int counter = 0;
    for (const auto* stmt : *m_statements)
    {
      printer->Write("Statement[%u]: ", counter++);
      stmt->Dump(printer);
    }
  }
  printer->EndBlock();
}

void PipelineAddStatement::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("PipelineAddStatement[filter=%s]", m_filter_name.c_str());
  if (m_filter_parameters)
  {
    unsigned int counter = 0;
    for (const auto* node : m_filter_parameters->GetNodeList())
    {
      printer->BeginBlock("Parameter[%u]", counter++);
      node->Dump(printer);
      printer->EndBlock();
    }
  }
  printer->EndBlock();
}

void IdentifierExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("IdentifierExpression(%s -> %s)", m_identifier.c_str(), m_type->GetName().c_str());
}

void BinaryExpression::Dump(ASTPrinter* printer) const
{
  std::array<const char*, 8> op_names = {
                        {"Add", "Subtract", "Multiply", "Divide", "Modulo", "BitwiseAnd", "BitwiseOr", "BitwiseXor"}};
  printer->BeginBlock("BinaryExpression(%s %s %s -> %s)", m_lhs->GetType()->GetName().c_str(), op_names[m_op],
                      m_rhs->GetType()->GetName().c_str(), m_type->GetName().c_str());
  printer->Write("lhs: ");
  m_lhs->Dump(printer);
  printer->Write("rhs: ");
  m_rhs->Dump(printer);
  printer->EndBlock();
}

void RelationalExpression::Dump(ASTPrinter* printer) const
{
  std::array<const char*, 6> op_names = {{"Less", "LessEqual", "Greater", "GreaterEqual", "Equal", "NotEqual"}};
  printer->BeginBlock("RelationalExpression(%s %s %s -> %s)", m_lhs->GetType()->GetName().c_str(), op_names[m_op],
                      m_rhs->GetType()->GetName().c_str(), m_type->GetName().c_str());
  printer->Write("lhs: ");
  m_lhs->Dump(printer);
  printer->Write("rhs: ");
  m_rhs->Dump(printer);
  printer->EndBlock();
}

void LogicalExpression::Dump(ASTPrinter* printer) const
{
  std::array<const char*, 3> op_names = {{"And", "Or", "Not"}};
  printer->BeginBlock("LogicalExpression(%s %s %s -> %s)", m_lhs->GetType()->GetName().c_str(), op_names[m_op],
                      m_rhs->GetType()->GetName().c_str(), m_type->GetName().c_str());
  printer->Write("lhs: ");
  m_lhs->Dump(printer);
  printer->Write("rhs: ");
  m_rhs->Dump(printer);
  printer->EndBlock();
}

void CommaExpression::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("CommaExpression");
  printer->Write("lhs (ignored): ");
  m_lhs->Dump(printer);
  printer->Write("rhs: ");
  m_rhs->Dump(printer);
  printer->EndBlock();
}

void AssignmentExpression::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("AssignmentExpression(%s -> %s)", m_rhs->GetType()->GetName().c_str(), m_type->GetName().c_str());
  printer->WriteLine("identifier: %s", m_identifier_declaration->GetName().c_str());
  printer->Write("rhs: ");
  m_rhs->Dump(printer);
  printer->EndBlock();
}

void IntegerLiteralExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("IntegerLiteralExpression(%d)", m_value);
}

void BooleanLiteralExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("BooleanLiteralExpression(%s)", m_value ? "true" : "false");
}

void PeekExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("PEEK: ");
  m_expr->Dump(printer);
}

void PopExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("POP");
}

void PushExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("PUSH: ");
  m_expr->Dump(printer);
}

void VariableDeclaration::Dump(ASTPrinter* printer) const
{
  if (!m_initializer)
  {
    printer->WriteLine("VariableDeclaration[name: %s, type: %s]", m_name.c_str(), m_type->GetName().c_str());
    return;
  }

  printer->BeginBlock("VariableDeclaration[name: %s, type: %s]", m_name.c_str(), m_type->GetName().c_str());
  printer->Write("initializer: ");
  m_initializer->Dump(printer);
  printer->EndBlock();
}

void FilterDeclaration::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("Filter[name=%s, input=%s, output=%s]", m_name.c_str(), m_input_type->GetName().c_str(),
                      m_output_type->GetName().c_str());
  {
    if (m_vars)
    {
      printer->BeginBlock("vars: ");

      unsigned int counter = 0;
      for (const auto* stmt : *m_vars)
      {
        printer->Write("var[%u]: ", counter++);
        stmt->Dump(printer);
      }
    }
    if (m_init)
    {
      printer->Write("init: ");
      m_init->Dump(printer);
    }
    if (m_prework)
    {
      printer->Write("prework: ");
      m_prework->Dump(printer);
    }
    if (m_work)
    {
      printer->Write("work: ");
      m_work->Dump(printer);
    }
  }
  printer->EndBlock();
}

void FilterWorkBlock::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("WorkBlock[peek=%d,pop=%d,push=%d]", m_peek_rate, m_pop_rate, m_push_rate);

  unsigned int counter = 0;
  for (const auto* stmt : *m_stmts)
  {
    printer->Write("Statement[%u]: ", counter++);
    stmt->Dump(printer);
  }

  printer->EndBlock();
}

void ExpressionStatement::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("ExpressionStatement: ");
  m_expr->Dump(printer);
}

void IfStatement::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("IfStatement");
  printer->Write("expr: ");
  m_expr->Dump(printer);
  printer->Write("then: ");
  m_then->Dump(printer);
  if (m_else)
  {
    printer->Write("else: ");
    m_else->Dump(printer);
  }
  printer->EndBlock();
}

void ForStatement::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("ForStatement");
  if (m_init)
  {
    printer->Write("init: ");
    m_init->Dump(printer);
  }
  if (m_cond)
  {
    printer->Write("cond: ");
    m_cond->Dump(printer);
  }
  if (m_loop)
  {
    printer->Write("loop: ");
    m_loop->Dump(printer);
  }
  if (m_inner)
  {
    printer->Write("inner: ");
    m_init->Dump(printer);
  }
  printer->EndBlock();
}

void BreakStatement::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("BreakStatement");
}

void ContinueStatement::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("ContinueStatement");
}

} // namespace AST