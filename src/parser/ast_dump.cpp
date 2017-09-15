#include "parser/ast.h"
#include <array>
#include <cassert>
#include "core/type.h"
#include "parser/ast_printer.h"

namespace AST
{
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

void TypeName::Dump(ASTPrinter* printer) const
{
  printer->Write("TypeSpecifier[base=%s", m_base_type_name.c_str());
  if (!m_array_sizes.empty())
  {
    printer->Write(",array=");
    for (Expression* expr : m_array_sizes)
    {
      printer->Write("[");
      expr->Dump(printer);
      printer->Write("]");
    }
  }
  printer->WriteLine("");
}

void StructSpecifier::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("StructSpecifier[name=%s]", m_name.c_str());
  for (const auto& it : m_fields)
  {
    printer->Write("field %s: ", it.first.c_str());
    it.second->Dump(printer);
  }
}

void ParameterDeclaration::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("ParameterDeclaration[type=%s,name=%s]", m_type->GetName().c_str(), m_name.c_str());
}

void PipelineDeclaration::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("PipelineDeclaration[name=%s, input=%s, output=%s]", m_name.c_str(),
                      m_input_type->GetName().c_str(), m_output_type->GetName().c_str());
  if (m_parameters)
  {
    unsigned int counter = 0;
    for (const auto* param : *m_parameters)
    {
      printer->Write("Parameter[%u]: ", counter++);
      param->Dump(printer);
    }
  }
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

void SplitJoinDeclaration::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("SplitJoinDeclaration[name=%s, input=%s, output=%s]", m_name.c_str(),
                      m_input_type->GetName().c_str(), m_output_type->GetName().c_str());
  if (m_parameters)
  {
    unsigned int counter = 0;
    for (const auto* param : *m_parameters)
    {
      printer->Write("Parameter[%u]: ", counter++);
      param->Dump(printer);
    }
  }
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

void FunctionDeclaration::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("FunctionDeclaration[name=%s]", m_name.c_str());
  // for (const Type* param_type : m_parameter_types)
  // printer->WriteLine("Parameter: %s", param_type->GetName().c_str());
  printer->EndBlock();
}

void AddStatement::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("AddStatement[filter=%s]", m_stream_name.c_str());
  if (m_stream_parameters)
  {
    unsigned int counter = 0;
    for (const auto* node : m_stream_parameters->GetNodeList())
    {
      printer->BeginBlock("Parameter[%u]", counter++);
      node->Dump(printer);
      printer->EndBlock();
    }
  }
  printer->EndBlock();
}

void SplitStatement::Dump(ASTPrinter* printer) const
{
  static const std::array<const char*, 2> type_names = {{"RoundRobin", "Duplicate"}};
  printer->WriteLine("SplitStatement[type=%s]", type_names[m_type]);
}

void JoinStatement::Dump(ASTPrinter* printer) const
{
  static const std::array<const char*, 1> type_names = {{"RoundRobin"}};
  printer->WriteLine("JoinStatement[type=%s]", type_names[m_type]);
}

void IdentifierExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("IdentifierExpression(%s -> %s)", m_identifier.c_str(), m_type->GetName().c_str());
}

void IndexExpression::Dump(ASTPrinter* printer) const
{
  printer->Write("IndexExpression(array: ");
  m_array_expression->Dump(printer);
  printer->Write(", index: ");
  m_index_expression->Dump(printer);
  printer->WriteLine("");
}

void UnaryExpression::Dump(ASTPrinter* printer) const
{
  static const std::array<const char*, 8> op_names = {{"PreIncrement", "PreDecrement", "PostIncrement", "PostDecrement",
                                                       "Positive", "Negative", "LogicalNot", "BitwiseNot"}};
  printer->BeginBlock("UnaryExpression(%s %s -> %s)", op_names[m_op], m_rhs->GetType()->GetName().c_str(),
                      m_type->GetName().c_str());
  printer->Write("rhs: ");
  m_rhs->Dump(printer);
  printer->EndBlock();
}

void BinaryExpression::Dump(ASTPrinter* printer) const
{
  static const std::array<const char*, 10> op_names = {{"Add", "Subtract", "Multiply", "Divide", "Modulo", "BitwiseAnd",
                                                        "BitwiseOr", "BitwiseXor", "LeftShift", "RightShift"}};
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
  static const std::array<const char*, 6> op_names = {
    {"Less", "LessEqual", "Greater", "GreaterEqual", "Equal", "NotEqual"}};
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
  static const std::array<const char*, 3> op_names = {{"And", "Or", "Not"}};
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
  static const std::array<const char*, 11> op_names = {{"Assign", "Add", "Subtract", "Multiply", "Divide", "Modulo",
                                                        "BitwiseAnd", "BitwiseOr", "BitwiseXor", "LeftShift",
                                                        "RightShift"}};
  printer->BeginBlock("AssignmentExpression(%s -> %s)", m_rhs->GetType()->GetName().c_str(), m_type->GetName().c_str());
  printer->Write("lhs: ");
  m_lhs->Dump(printer);
  printer->Write("op: %s", op_names[m_op]);
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
  printer->Write("PeekExpression[index=");
  m_expr->Dump(printer);
  printer->WriteLine("]");
}

void PopExpression::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("PopExpression");
}

void CallExpression::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("CallExpression: ");
  printer->WriteLine("function_name = %s (%s)", m_function_name.c_str(),
                     m_function_ref ? m_function_ref->GetSymbolName().c_str() : "<unresolved>");
  if (m_args)
  {
    printer->Write("parameters = ");
    m_args->Dump(printer);
  }
  printer->EndBlock();
}

void CastExpression::Dump(ASTPrinter* printer) const
{
  printer->BeginBlock("CastExpression: ");
  printer->Write("ToType = %s, Expression = ", m_type->GetName().c_str());
  m_expr->Dump(printer);
  printer->EndBlock();
}

void PushStatement::Dump(ASTPrinter* printer) const
{
  printer->Write("PushExpression[value=");
  m_expr->Dump(printer);
  printer->WriteLine("]");
}

void InitializerListExpression::Dump(ASTPrinter* printer) const
{
  printer->Write("InitializerListExpression[%d] = {", static_cast<int>(m_expressions.size()));

  bool first = true;
  for (Expression* expr : m_expressions)
  {
    if (!first)
      printer->Write(", ");
    else
      first = false;

    expr->Dump(printer);
  }

  printer->WriteLine("}");
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
    if (m_parameters)
    {
      unsigned int counter = 0;
      for (const auto* param : *m_parameters)
      {
        printer->Write("Parameter[%u]: ", counter++);
        param->Dump(printer);
      }
    }
    if (m_vars)
    {
      printer->BeginBlock("vars: ");

      unsigned int counter = 0;
      for (const auto* stmt : *m_vars)
      {
        printer->Write("var[%u]: ", counter++);
        stmt->Dump(printer);
      }
      printer->EndBlock();
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
  printer->BeginBlock("WorkBlock");
  if (m_peek_rate_expr)
  {
    printer->Write("peek: ");
    m_peek_rate_expr->Dump(printer);
  }
  if (m_pop_rate_expr)
  {
    printer->Write("pop: ");
    m_pop_rate_expr->Dump(printer);
  }
  if (m_push_rate_expr)
  {
    printer->Write("push: ");
    m_push_rate_expr->Dump(printer);
  }

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

void ReturnStatement::Dump(ASTPrinter* printer) const
{
  printer->WriteLine("ReturnStatement");
}

} // namespace AST