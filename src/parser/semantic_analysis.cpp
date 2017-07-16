#include <algorithm>
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

bool TypeName::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  Node* node_ptr = symbol_table->GetName(m_base_type_name);
  TypeReference* type_ref_ptr = dynamic_cast<TypeReference*>(node_ptr);
  if (!type_ref_ptr || !type_ref_ptr->GetType()->IsValid())
  {
    state->ReportError(m_sloc, "Unknown type '%s'", m_base_type_name.c_str());
    m_final_type = state->GetErrorType();
    return false;
  }

  if (m_array_sizes.empty())
  {
    // Non-array types can just be forwarded through
    m_final_type = type_ref_ptr->GetType();
  }
  else
  {
    // Array types have to have the array sizes annotated
    m_final_type = state->GetArrayType(type_ref_ptr->GetType(), m_array_sizes);
  }

  return m_final_type->IsValid();
}

bool StructSpecifier::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;

  // Check all the field types
  for (auto& it : m_fields)
    result &= it.second->SemanticAnalysis(state, symbol_table);

  // Check if the name already exists
  if (symbol_table->HasName(m_name))
  {
    state->ReportError(m_sloc, "Structure '%s' already defined", m_name.c_str());
    result = false;
  }

  // Check for duplicate field names
  for (const auto& field1 : m_fields)
  {
    if (std::any_of(m_fields.begin(), m_fields.end(),
                    [&field1](const auto& field2) { return field1.first == field2.first; }))
    {
      state->ReportError(m_sloc, "Duplicate field name '%s'", field1.first.c_str());
      result = false;
    }
  }

  if (!result)
  {
    m_final_type = state->GetErrorType();
    return false;
  }

  // TODO: Calculate final type
  m_final_type = state->GetErrorType();

  // Insert into symbol table
  return symbol_table->AddName(m_name, new TypeReference(m_name, m_final_type));
}

bool PipelineDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_input_type_specifier->SemanticAnalysis(state, symbol_table);
  m_input_type = m_input_type_specifier->GetFinalType();
  result &= m_output_type_specifier->SemanticAnalysis(state, symbol_table);
  m_output_type = m_output_type_specifier->GetFinalType();

  if (m_statements)
    result &= m_statements->SemanticAnalysis(state, symbol_table);

  if (!symbol_table->AddName(m_name, this))
  {
    state->ReportError(m_sloc, "Pipeline '%s' is already defined", m_name.c_str());
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
    state->ReportError(m_sloc, "Referencing undefined filter/pipeline '%s'", m_filter_name.c_str());
    return false;
  }

  return true;
}

bool FilterDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  assert(!state->current_filter);
  state->current_filter = this;

  result &= m_input_type_specifier->SemanticAnalysis(state, symbol_table);
  m_input_type = m_input_type_specifier->GetFinalType();
  result &= m_output_type_specifier->SemanticAnalysis(state, symbol_table);
  m_output_type = m_output_type_specifier->GetFinalType();

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
    state->ReportError(m_sloc, "Filter '%s' already defined", m_name.c_str());
    result = false;
  }

  // For LLVM codegen at least, it's easier to have all the initialization happen in init.
  if (result)
    MoveStateAssignmentsToInit();

  assert(state->current_filter == this);
  state->current_filter = nullptr;
  return result;
}

void FilterDeclaration::MoveStateAssignmentsToInit()
{
  if (!m_vars->HasChildren())
    return;

  std::unique_ptr<NodeList> assign_exprs = std::make_unique<NodeList>();
  for (Node* node : m_vars->GetNodeList())
  {
    VariableDeclaration* var_decl = dynamic_cast<VariableDeclaration*>(node);
    if (!var_decl || !var_decl->HasInitializer() || var_decl->GetInitializer()->IsConstant())
      continue;

    assign_exprs->AddNode(new AssignmentExpression(
                          var_decl->GetSourceLocation(),
                          new AST::IdentifierExpression(var_decl->GetSourceLocation(), var_decl->GetName().c_str()),
                          var_decl->GetInitializer()));
    var_decl->RemoveInitializer();
  }

  if (!assign_exprs->HasChildren())
    return;

  if (!m_init)
  {
    m_init = new FilterWorkBlock();
    m_init->SetStatements(assign_exprs.release());
  }
  else
  {
    m_init->GetStatements()->PrependNode(assign_exprs.get());
  }
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
    state->ReportError(m_sloc, "Unknown identifier '%s'", m_identifier.c_str());
    return false;
  }

  m_type = m_identifier_declaration->GetType();
  return m_type->IsValid();
}

bool IndexExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_array_expression->SemanticAnalysis(state, symbol_table);
  result &= m_index_expression->SemanticAnalysis(state, symbol_table);
  m_type = Type::GetArrayElementType(state, m_array_expression->GetType());
  return result && m_type->IsValid();
}

bool BinaryExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // Calculate type of expression
  m_type = Type::GetResultType(state, m_lhs->GetType(), m_rhs->GetType());
  if (!m_type->IsValid())
  {
    state->ReportError(m_sloc, "Cannot determine result type for binary expression of %s and %s",
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
  m_intermediate_type = Type::GetResultType(state, m_lhs->GetType(), m_rhs->GetType());
  if (!m_intermediate_type)
  {
    state->ReportError(m_sloc, "Cannot determine comparison type of relational expression between %s and %s",
                       m_lhs->GetType()->GetName().c_str(), m_rhs->GetType()->GetName().c_str());
    return false;
  }

  // Less/Greater are only valid on numeric types
  if (m_op >= Less && m_op <= GreaterEqual && !m_intermediate_type->IsInt() && !m_intermediate_type->IsFloat())
  {
    state->ReportError(m_sloc, "Relational operators are only valid on numeric types");
    return false;
  }

  // Result is a boolean type
  m_type = state->GetBooleanType();
  return true;
}

bool LogicalExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // TODO: Can you use C-like if (1) ?
  if (!m_lhs->GetType()->IsBoolean() || !m_rhs->GetType()->IsBoolean())
  {
    state->ReportError(m_sloc, "Logical expression must be boolean types on both sides");
    return false;
  }

  // Result is also a boolean type
  m_type = state->GetBooleanType();
  return true;
}

bool CommaExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_lhs->SemanticAnalysis(state, symbol_table);
  result &= m_rhs->SemanticAnalysis(state, symbol_table);

  // result is rhs and type of rhs
  m_type = m_rhs->GetType();
  return result && m_type->IsValid();
}

bool AssignmentExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  if (!m_rhs->GetType()->CanImplicitlyConvertTo(m_lhs->GetType()))
  {
    state->ReportError(m_sloc, "Cannot implicitly convert from '%s' to '%s'", m_rhs->GetType()->GetName().c_str(),
                       m_lhs->GetType()->GetName().c_str());
    return false;
  }

  m_type = m_lhs->GetType();
  return m_type->IsValid();
}

bool IntegerLiteralExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->GetIntType();
  return true;
}

bool BooleanLiteralExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->GetBooleanType();
  return true;
}

bool PeekExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = m_expr->SemanticAnalysis(state, symbol_table);
  m_type = state->current_filter->GetInputType();

  if (!m_expr->IsConstant())
  {
    state->ReportError(m_sloc, "Peek offset is not constant.");
    result = false;
  }

  return result && m_type->IsValid();
}

bool PopExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->current_filter->GetInputType();
  return m_type->IsValid();
}

bool PushStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = m_expr->SemanticAnalysis(state, symbol_table);

  const Type* filter_output_type = state->current_filter->GetOutputType();
  if (!m_expr->GetType()->CanImplicitlyConvertTo(filter_output_type))
  {
    state->ReportError(m_sloc, "Cannot implicitly convert between '%s' and '%s'", m_expr->GetType()->GetName().c_str(),
                       filter_output_type->GetName().c_str());
    result = false;
  }

  return result;
}

bool VariableDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_type_specifier->SemanticAnalysis(state, symbol_table);
  m_type = m_type_specifier->GetFinalType();

  if (symbol_table->HasName(m_name))
  {
    state->ReportError(m_sloc, "Duplicate definition of '%s'", m_name.c_str());
    result = false;
  }

  if (m_initializer)
  {
    if (!m_initializer->SemanticAnalysis(state, symbol_table))
      result = false;

    if (!m_initializer->GetType()->CanImplicitlyConvertTo(m_type))
    {
      state->ReportError(m_sloc, "Cannot implicitly convert from '%s' to '%s'",
                         m_initializer->GetType()->GetName().c_str(), m_type->GetName().c_str());
      result = false;
    }
  }

  if (!symbol_table->AddName(m_name, this))
  {
    state->ReportError(m_sloc, "Variable '%s' already defined", m_name.c_str());
    result = false;
  }

  return result;
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

  if (!m_expr->GetType()->IsBoolean())
  {
    state->ReportError(m_sloc, "If expression must be a boolean type");
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
  if (m_cond && !m_cond->GetType()->IsBoolean())
  {
    state->ReportError(m_sloc, "Loop condition must be boolean");
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

bool ReturnStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO: Check if we're in a function, and the return type matches
  bool result = true;
  result &= (!m_expr || m_expr->SemanticAnalysis(state, symbol_table));
  return result;
}

} // namespace AST
