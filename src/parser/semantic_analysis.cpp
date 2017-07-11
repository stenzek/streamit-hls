#include <cassert>
#include "parser/ast.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
#include "parser/type.h"

namespace AST
{
bool Program::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;

  // Resolve filters before pipelines, since pipelines can appear before the filters that they are comprised of.
  for (auto* filter : m_filters)
    result &= filter->SemanticAnalysis(state, symbol_table);

  for (auto* pipeline : m_pipelines)
    result &= pipeline->SemanticAnalysis(state, symbol_table);

  return result;
}

bool NodeList::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  for (Node* node : m_nodes)
    result &= node->SemanticAnalysis(state, symbol_table);
  return result;
}

bool PipelineDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  if (m_statements)
    result &= m_statements->SemanticAnalysis(state, symbol_table);

  if (!symbol_table->AddName(m_name, this))
  {
    state->ReportError("Pipeline '%s' is already defined", m_name.c_str());
    return false;
  }

  return result;
}

bool PipelineAddStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO: Create variable declarations for parameters - we can manipulate these when generating code..
  // TODO: Check parameter counts and stuff..
  // TODO: Check return types
  m_filter_declaration = symbol_table->GetName(m_filter_name);
  if (!m_filter_declaration)
  {
    state->ReportError("Referencing undefined filter/pipeline '%s'", m_filter_name.c_str());
    return false;
  }

  return true;
}

bool FilterDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  assert(!state->current_filter);
  state->current_filter = this;

  // Each filter has its own symbol table (stateful stuff), then each part has its own symbol table
  LexicalScope filter_symbol_table(symbol_table);
  if (m_vars)
    result &= m_vars->SemanticAnalysis(state, &filter_symbol_table);

  if (m_init)
  {
    LexicalScope init_symbol_table(&filter_symbol_table);
    result &= m_init->SemanticAnalysis(state, &init_symbol_table);
  }
  if (m_prework)
  {
    LexicalScope prework_symbol_table(&filter_symbol_table);
    result &= m_prework->SemanticAnalysis(state, &prework_symbol_table);
  }
  if (m_work)
  {
    LexicalScope work_symbol_table(&filter_symbol_table);
    result &= m_work->SemanticAnalysis(state, &work_symbol_table);
  }

  // Add the filter into the global namespace
  if (!symbol_table->AddName(m_name, this))
  {
    state->ReportError("Filter '%s' already defined", m_name.c_str());
    result = false;
  }

  assert(state->current_filter == this);
  state->current_filter = nullptr;
  return result;
}

bool FilterWorkBlock::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO: Check rates and stuff (e.g. push type matches output type)..
  return m_stmts->SemanticAnalysis(state, symbol_table);
}

bool IdentifierExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve identifier
  m_identifier_declaration = dynamic_cast<VariableDeclaration*>(symbol_table->GetName(m_identifier));
  if (!m_identifier_declaration)
  {
    state->ReportError("Unknown identifier '%s'", m_identifier.c_str());
    return false;
  }

  m_type = m_identifier_declaration->GetType();
  return m_type->IsValid();
}

bool BinaryExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // Calculate type of expression
  m_type = Type::GetResultType(m_lhs->GetType(), m_rhs->GetType());
  if (!m_type->IsValid())
  {
    state->ReportError("Cannot determine result type for binary expression of %s and %s",
                       m_lhs->GetType()->GetName().c_str(), m_rhs->GetType()->GetName().c_str());
    return false;
  }

  return m_type->IsValid();
}

bool RelationalExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // Implicit conversions..
  m_intermediate_type = Type::GetResultType(m_lhs->GetType(), m_rhs->GetType());
  if (!m_intermediate_type)
  {
    state->ReportError("Cannot determine comparison type of relational expression between %s and %s",
                       m_lhs->GetType()->GetName().c_str(), m_rhs->GetType()->GetName().c_str());
    return false;
  }

  // Less/Greater are only valid on numeric types
  if (m_op >= Less && m_op <= GreaterEqual &&
      (m_intermediate_type != Type::GetIntType() && m_intermediate_type != Type::GetFloatType()))
  {
    state->ReportError("Relational operators are only valid on numeric types");
    return false;
  }

  // Result is a boolean type
  m_type = Type::GetBooleanType();
  return true;
}

bool LogicalExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // TODO: Can you use C-like if (1) ?
  if (m_lhs->GetType() != Type::GetBooleanType() || m_rhs->GetType() != Type::GetBooleanType())
  {
    state->ReportError("Logical expression must be boolean types on both sides");
    return false;
  }

  // Result is also a boolean type
  m_type = Type::GetBooleanType();
  return true;
}

bool AssignmentExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // Left hand side should be an identifier
  IdentifierExpression* lhs_identifier_expression = dynamic_cast<IdentifierExpression*>(m_lhs);
  if (!lhs_identifier_expression)
  {
    state->ReportError("Left hand side of expression must be an identifier");
    return false;
  }

  // Just grab the identifier straight from the expression
  m_identifier_declaration = lhs_identifier_expression->GetReferencedVariable();
  if (!m_identifier_declaration)
    return false;

  if (!m_rhs->GetType()->CanImplicitlyConvertTo(m_identifier_declaration->GetType()))
  {
    state->ReportError("Cannot implicitly convert from '%s' to '%s'", m_rhs->GetType()->GetName().c_str(),
                       m_identifier_declaration->GetType()->GetName().c_str());
    return false;
  }

  m_type = m_identifier_declaration->GetType();
  return m_type->IsValid();
}

bool IntegerLiteralExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  return true;
}

bool BooleanLiteralExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  return true;
}

bool PeekExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = m_expr->SemanticAnalysis(state, symbol_table);
  m_type = state->current_filter->GetInputType();

  if (!m_expr->IsConstant())
  {
    state->ReportError("Peek offset is not constant.");
    result = false;
  }

  return result && m_type->IsValid();
}

bool PopExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->current_filter->GetInputType();
  return m_type->IsValid();
}

bool PushExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = m_expr->SemanticAnalysis(state, symbol_table);

  m_type = state->current_filter->GetOutputType();
  if (!m_expr->GetType()->CanImplicitlyConvertTo(m_type))
  {
    state->ReportError("Cannot implicitly convert between '%s' and '%s'", m_expr->GetType()->GetName().c_str(),
                       m_type->GetName().c_str());
    result = false;
  }

  return result && m_type->IsValid();
}

bool VariableDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  if (symbol_table->HasName(m_name))
  {
    state->ReportError("Duplicate definition of '%s'", m_name.c_str());
    return false;
  }

  if (m_initializer)
  {
    if (!m_initializer->SemanticAnalysis(state, symbol_table))
      return false;

    if (!m_initializer->GetType()->CanImplicitlyConvertTo(m_type))
    {
      state->ReportError("Cannot implicitly convert from '%s' to '%s'", m_initializer->GetType()->GetName().c_str(),
                         m_type->GetName().c_str());
      return false;
    }
  }

  if (!symbol_table->AddName(m_name, this))
  {
    state->ReportError("Variable '%s' already defined", m_name.c_str());
    return false;
  }

  return true;
}

bool ExpressionStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  return m_expr->SemanticAnalysis(state, symbol_table);
}

bool IfStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_expr->SemanticAnalysis(state, symbol_table);
  result &= m_then->SemanticAnalysis(state, symbol_table);
  result &= (!m_else || m_else->SemanticAnalysis(state, symbol_table));

  if (m_expr->GetType() != Type::GetBooleanType())
  {
    state->ReportError("If expression must be a boolean type");
    return false;
  }

  return result;
}

bool ForStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= (!m_init || m_init->SemanticAnalysis(state, symbol_table));
  result &= (!m_cond || m_cond->SemanticAnalysis(state, symbol_table));
  result &= (!m_loop || m_loop->SemanticAnalysis(state, symbol_table));
  result &= (!m_inner || m_inner->SemanticAnalysis(state, symbol_table));

  // Condition should be non-existant, or a boolean
  if (m_cond && m_cond->GetType() != Type::GetBooleanType())
  {
    state->ReportError("Loop condition must be boolean");
    result = false;
  }

  return result;
}

bool BreakStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO: Check if we're in a loop - otherwise it'll assert in the frontend
  return true;
}

bool ContinueStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO: Check if we're in a loop - otherwise it'll assert in the frontend
  return true;
}

} // namespace AST
