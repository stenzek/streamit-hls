#include <algorithm>
#include <cassert>
#include <sstream>
#include "common/log.h"
#include "core/type.h"
#include "parser/ast.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
Log_SetChannel(Parser);

namespace AST
{
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
    state->LogError(m_sloc, "Unknown type '%s'", m_base_type_name.c_str());
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
    state->LogError(m_sloc, "Structure '%s' already defined", m_name.c_str());
    result = false;
  }

  // Check for duplicate field names
  for (const auto& field1 : m_fields)
  {
    if (std::any_of(m_fields.begin(), m_fields.end(),
                    [&field1](const auto& field2) { return field1.first == field2.first; }))
    {
      state->LogError(m_sloc, "Duplicate field name '%s'", field1.first.c_str());
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

bool ParameterDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = m_type_specifier->SemanticAnalysis(state, symbol_table);
  m_type = m_type_specifier->GetFinalType();

  if (!symbol_table->AddName(m_name, this))
  {
    state->LogError(m_sloc, "Variable '%s' already defined", m_name.c_str());
    result = false;
  }

  return result;
}

bool PipelineDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;

  LexicalScope pipeline_scope(symbol_table);
  StreamDeclaration* old_current_stream = state->current_stream;
  LexicalScope* old_current_stream_scope = state->current_stream_scope;
  state->current_stream = this;
  state->current_stream_scope = &pipeline_scope;

  if (m_input_type_specifier && m_output_type_specifier)
  {
    result &= m_input_type_specifier->SemanticAnalysis(state, &pipeline_scope);
    m_input_type = m_input_type_specifier->GetFinalType();
    result &= m_output_type_specifier->SemanticAnalysis(state, &pipeline_scope);
    m_output_type = m_output_type_specifier->GetFinalType();
  }
  else
  {
    // TODO: Anonymous stream
    m_input_type = state->GetErrorType();
    m_output_type = state->GetErrorType();
  }

  if (m_parameters)
  {
    for (ParameterDeclaration* param : *m_parameters)
      result &= param->SemanticAnalysis(state, &pipeline_scope);
  }

  if (m_statements)
    result &= m_statements->SemanticAnalysis(state, &pipeline_scope);

  assert(state->current_stream == this);
  state->current_stream = old_current_stream;
  state->current_stream_scope = old_current_stream_scope;

  return result;
}

bool SplitJoinDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;

  LexicalScope splitjoin_scope(symbol_table);
  StreamDeclaration* old_current_stream = state->current_stream;
  LexicalScope* old_current_stream_scope = state->current_stream_scope;
  state->current_stream = this;
  state->current_stream_scope = &splitjoin_scope;

  if (m_input_type_specifier && m_output_type_specifier)
  {
    result &= m_input_type_specifier->SemanticAnalysis(state, &splitjoin_scope);
    m_input_type = m_input_type_specifier->GetFinalType();
    result &= m_output_type_specifier->SemanticAnalysis(state, &splitjoin_scope);
    m_output_type = m_output_type_specifier->GetFinalType();
  }
  else
  {
    // TODO: Anonymous stream
    m_input_type = state->GetErrorType();
    m_output_type = state->GetErrorType();
  }

  if (m_parameters)
  {
    for (ParameterDeclaration* param : *m_parameters)
      result &= param->SemanticAnalysis(state, &splitjoin_scope);
  }

  if (m_statements)
    result &= m_statements->SemanticAnalysis(state, &splitjoin_scope);

  assert(state->current_stream == this);
  state->current_stream = old_current_stream;
  state->current_stream_scope = old_current_stream_scope;

  return result;
}

bool AddStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO: Create variable declarations for parameters - we can manipulate these when generating code..
  // TODO: Check parameter counts and stuff..
  // TODO: Check return types
  m_stream_declaration = dynamic_cast<StreamDeclaration*>(symbol_table->GetName(m_stream_name));
  if (!m_stream_declaration)
  {
    state->LogError(m_sloc, "Referencing undefined filter/pipeline '%s'", m_stream_name.c_str());
    return false;
  }

  if (!state->HasActiveStream(m_stream_declaration))
  {
    if (!m_stream_parameters)
    {
      // This is an anonymous stream/filter.
      // Copy everything in the symbol table as parameters to the anonymous stream/filter.
      m_stream_parameters = new NodeList();

      // Start at the current scope, and move up until we hit the current stream.
      LexicalScope* current_scope = symbol_table;
      while (current_scope != nullptr)
      {
        for (const auto& it : *current_scope)
        {
          Declaration* decl = dynamic_cast<Declaration*>(it.second);
          if (!decl)
            continue;

          // TODO: Filter types to capture?
          // Log_DevPrintf("Capture %s %s", decl->GetType()->GetName().c_str(), decl->GetName().c_str());

          // Add parameter to stream.
          ParameterDeclaration* param_decl =
            new ParameterDeclaration(decl->GetSourceLocation(), new TypeName(decl->GetType()), decl->GetName());
          m_stream_declaration->GetParameters()->push_back(param_decl);

          // Add reference to add parameters.
          m_stream_parameters->AddNode(new IdentifierExpression(m_sloc, it.first.c_str()));
        }

        if (current_scope == state->current_stream_scope)
          break;

        current_scope = current_scope->GetParentScope();
      }
    }

    state->AddActiveStream(m_stream_declaration);
    if (!m_stream_declaration->SemanticAnalysis(state, symbol_table))
      return false;
  }

  if (!m_stream_parameters->SemanticAnalysis(state, symbol_table))
    return false;

  return true;
}

bool FunctionDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO
  return false;
}

bool SplitStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO
  return true;
}

bool JoinStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // TODO
  return true;
}

bool FilterDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;

  // Each filter has its own symbol table (stateful stuff), then each part has its own symbol table
  LexicalScope filter_symbol_table(symbol_table);
  StreamDeclaration* old_stream = state->current_stream;
  LexicalScope* old_stream_scope = state->current_stream_scope;
  state->current_stream = this;
  state->current_stream_scope = &filter_symbol_table;

  result &= m_input_type_specifier->SemanticAnalysis(state, &filter_symbol_table);
  m_input_type = m_input_type_specifier->GetFinalType();
  result &= m_output_type_specifier->SemanticAnalysis(state, &filter_symbol_table);
  m_output_type = m_output_type_specifier->GetFinalType();

  if (m_parameters)
  {
    for (ParameterDeclaration* param : *m_parameters)
      result &= param->SemanticAnalysis(state, &filter_symbol_table);
  }

  // For LLVM codegen at least, it's easier to have all the initialization happen in init.
  if (result)
    MoveStateAssignmentsToInit();

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

  assert(state->current_stream == this);
  state->current_stream = old_stream;
  state->current_stream_scope = old_stream_scope;
  return result;
}

void FilterDeclaration::MoveStateAssignmentsToInit()
{
  if (!m_vars || !m_vars->HasChildren())
    return;

  std::unique_ptr<NodeList> assign_exprs = std::make_unique<NodeList>();
  for (Node* node : m_vars->GetNodeList())
  {
    VariableDeclaration* var_decl = dynamic_cast<VariableDeclaration*>(node);
    if (!var_decl || !var_decl->HasInitializer() || var_decl->GetInitializer()->IsConstant())
      continue;

    assign_exprs->AddNode(
      new ExpressionStatement(var_decl->GetSourceLocation(),
                              new AssignmentExpression(var_decl->GetSourceLocation(),
                                                       new AST::IdentifierExpression(var_decl->GetSourceLocation(),
                                                                                     var_decl->GetName().c_str()),
                                                       AssignmentExpression::Assign, var_decl->GetInitializer())));
    var_decl->RemoveInitializer();
  }

  if (!assign_exprs->HasChildren())
    return;

  if (!m_init)
  {
    m_init = new FilterWorkBlock({});
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
  bool result = true;
  if (m_peek_rate_expr && (result &= m_peek_rate_expr->SemanticAnalysis(state, symbol_table)) &&
      !m_peek_rate_expr->GetType()->IsInt())
  {
    state->LogError(m_sloc, "Peek rate is not an integer");
    result = false;
  }
  if (m_pop_rate_expr && (result &= m_pop_rate_expr->SemanticAnalysis(state, symbol_table)) &&
      !m_pop_rate_expr->GetType()->IsInt())
  {
    state->LogError(m_sloc, "Pop rate is not an integer");
    result = false;
  }
  if (m_push_rate_expr && (result &= m_push_rate_expr->SemanticAnalysis(state, symbol_table)) &&
      !m_push_rate_expr->GetType()->IsInt())
  {
    state->LogError(m_sloc, "Push rate is not an integer");
    result = false;
  }

  result &= m_stmts->SemanticAnalysis(state, symbol_table);
  return result;
}

bool IdentifierExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Resolve identifier
  m_declaration = dynamic_cast<Declaration*>(symbol_table->GetName(m_identifier));
  if (!m_declaration)
  {
    state->LogError(m_sloc, "Unknown identifier '%s'", m_identifier.c_str());
    return false;
  }

  m_type = m_declaration->GetType();
  return m_type->IsValid();
}

bool IndexExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_array_expression->SemanticAnalysis(state, symbol_table);
  result &= m_index_expression->SemanticAnalysis(state, symbol_table);

  if (!m_array_expression->GetType()->IsArrayType())
  {
    state->LogError(m_sloc, "Array expression is not an array type");
    m_type = state->GetErrorType();
    return false;
  }

  m_type = state->GetArrayElementType(static_cast<const ArrayType*>(m_array_expression->GetType()));
  return result && m_type->IsValid();
}

bool UnaryExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_rhs->SemanticAnalysis(state, symbol_table);

  // Same as the rhs in most cases?
  m_type = m_rhs->GetType();
  return m_type->IsValid();
}

bool BinaryExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->GetErrorType();

  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // Calculate type of expression
  m_type = state->GetResultType(m_lhs->GetType(), m_rhs->GetType());
  if (!m_type->IsValid())
  {
    state->LogError(m_sloc, "Cannot determine result type for binary expression of %s and %s",
                    m_lhs->GetType()->GetName().c_str(), m_rhs->GetType()->GetName().c_str());
    return false;
  }

  return m_type->IsValid();
}

bool RelationalExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->GetErrorType();
  m_intermediate_type = state->GetErrorType();

  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // Implicit conversions..
  m_intermediate_type = state->GetResultType(m_lhs->GetType(), m_rhs->GetType());
  if (!m_intermediate_type)
  {
    state->LogError(m_sloc, "Cannot determine comparison type of relational expression between %s and %s",
                    m_lhs->GetType()->GetName().c_str(), m_rhs->GetType()->GetName().c_str());
    return false;
  }

  // Less/Greater are only valid on numeric types
  if (m_op >= Less && m_op <= GreaterEqual && !m_intermediate_type->IsInt() && !m_intermediate_type->IsFloat())
  {
    state->LogError(m_sloc, "Relational operators are only valid on numeric types");
    return false;
  }

  // Result is a boolean type
  m_type = state->GetBooleanType();
  return true;
}

bool LogicalExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->GetErrorType();

  // Resolve any identifiers
  if (!(m_lhs->SemanticAnalysis(state, symbol_table) & m_rhs->SemanticAnalysis(state, symbol_table)))
    return false;

  // TODO: Can you use C-like if (1) ?
  if (!m_lhs->GetType()->IsBoolean() || !m_rhs->GetType()->IsBoolean())
  {
    state->LogError(m_sloc, "Logical expression must be boolean types on both sides");
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
  bool result = true;
  result &= m_lhs->SemanticAnalysis(state, symbol_table);
  result &= m_rhs->SemanticAnalysis(state, symbol_table);

  if (result && !m_rhs->GetType()->CanImplicitlyConvertTo(m_lhs->GetType()))
  {
    state->LogError(m_sloc, "Cannot implicitly convert from '%s' to '%s'", m_rhs->GetType()->GetName().c_str(),
                    m_lhs->GetType()->GetName().c_str());
    result = false;
  }

  m_type = result ? m_lhs->GetType() : state->GetErrorType();
  return result && m_type->IsValid();
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
  m_type = state->current_stream->GetInputType();
  return result && m_type->IsValid();
}

bool PopExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  m_type = state->current_stream->GetInputType();
  return m_type->IsValid();
}

bool CallExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  // Generate the function name
  std::stringstream ss;
  ss << m_function_name;

  bool result = true;
  if (m_args)
  {
    for (Node* arg : *m_args)
    {
      result &= arg->SemanticAnalysis(state, symbol_table);

      Expression* expr = dynamic_cast<Expression*>(arg);
      if (!expr)
      {
        state->LogError(m_sloc, "Argument is not an expression");
        result = false;
        continue;
      }

      ss << "___" << expr->GetType()->GetName();
    }
  }

  std::string function_symbol_name = ss.str();
  m_function_ref = dynamic_cast<FunctionReference*>(symbol_table->GetName(function_symbol_name));
  if (!m_function_ref)
  {
    state->LogError("Can't find function '%s' with symbol name '%s'", m_function_name.c_str(),
                    function_symbol_name.c_str());
    return false;
  }

  m_type = m_function_ref->GetReturnType();
  return result;
}

FunctionReference::FunctionReference(const std::string& name, const Type* return_type,
                                     const std::vector<const Type*>& param_types, bool builtin)
  : m_name(name), m_function_type(FunctionType::Create(return_type, param_types)), m_return_type(return_type),
    m_param_types(param_types), m_builtin(builtin)
{
  // Generate symbol name
  std::stringstream ss;
  ss << m_name;
  for (const Type* param_ty : m_param_types)
    ss << "___" << param_ty->GetName();
  m_symbol_name = ss.str();
}

std::string FunctionReference::GetExecutableSymbolName() const
{
  if (!m_builtin)
    return m_symbol_name;

  return StringFromFormat("streamit_%s", m_symbol_name.c_str());
}

bool PushStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = m_expr->SemanticAnalysis(state, symbol_table);

  const Type* filter_output_type = state->current_stream->GetOutputType();
  if (!m_expr->GetType()->CanImplicitlyConvertTo(filter_output_type))
  {
    state->LogError(m_sloc, "Cannot implicitly convert between '%s' and '%s'", m_expr->GetType()->GetName().c_str(),
                    filter_output_type->GetName().c_str());
    result = false;
  }

  return result;
}

bool InitializerListExpression::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;

  // TODO: Expected type and implicit type conversions based on the left hand side of the expression.
  // Maybe this would be better off done in frontend, and just leaving all this as abstract expressions..
  for (size_t i = 0; i < m_expressions.size(); i++)
  {
    result &= m_expressions[i]->SemanticAnalysis(state, symbol_table);

    // All elements must be the same type
    if (i != 0 && m_expressions[i]->GetType() != m_expressions[0]->GetType())
    {
      state->LogError(m_sloc, "Initializer %d does not match the type of the list", static_cast<int>(i));
      result = false;
    }
  }

  if (!result)
  {
    m_type = state->GetErrorType();
    return false;
  }

  // Result is an array of the specified length.
  m_type = state->GetArrayType(m_expressions[0]->GetType(), {static_cast<int>(m_expressions.size())});
  return m_type->IsValid();
}

bool VariableDeclaration::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  result &= m_type_specifier->SemanticAnalysis(state, symbol_table);
  m_type = m_type_specifier->GetFinalType();

  if (symbol_table->HasNameInLocalScope(m_name))
  {
    state->LogError(m_sloc, "Duplicate definition of '%s'", m_name.c_str());
    result = false;
  }

  if (m_initializer)
  {
    if (!m_initializer->SemanticAnalysis(state, symbol_table))
      result = false;

    if (!m_initializer->GetType()->CanImplicitlyConvertTo(m_type))
    {
      state->LogError(m_sloc, "Cannot implicitly convert from '%s' to '%s'",
                      m_initializer->GetType()->GetName().c_str(), m_type->GetName().c_str());
      result = false;
    }
  }

  if (!symbol_table->AddName(m_name, this))
  {
    state->LogError(m_sloc, "Variable '%s' already defined", m_name.c_str());
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
  LexicalScope if_scope(symbol_table);
  result &= m_expr->SemanticAnalysis(state, &if_scope);
  result &= m_then->SemanticAnalysis(state, &if_scope);
  result &= (!m_else || m_else->SemanticAnalysis(state, &if_scope));

  if (!m_expr->GetType()->IsBoolean())
  {
    state->LogError(m_sloc, "If expression must be a boolean type");
    return false;
  }

  return result;
}

bool ForStatement::SemanticAnalysis(ParserState* state, LexicalScope* symbol_table)
{
  bool result = true;
  LexicalScope loop_scope(symbol_table);
  result &= (!m_init || m_init->SemanticAnalysis(state, &loop_scope));
  result &= (!m_cond || m_cond->SemanticAnalysis(state, &loop_scope));
  result &= (!m_loop || m_loop->SemanticAnalysis(state, &loop_scope));
  result &= (!m_inner || m_inner->SemanticAnalysis(state, &loop_scope));

  // Condition should be non-existant, or a boolean
  if (m_cond && !m_cond->GetType()->IsBoolean())
  {
    state->LogError(m_sloc, "Loop condition must be boolean");
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
