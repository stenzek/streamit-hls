// clang-format off
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include "parser/ast.h"
#include "parser/parser_defines.h"
#include "parser/parser_tokens.h"
#include "parser/parser_state.h"
// clang-format on

extern FILE* yyin;
int yyparse(ParserState* state);

ParserState::ParserState()
{
  m_global_lexical_scope = std::make_unique<AST::LexicalScope>(nullptr);
  CreateBuiltinTypes();
}

ParserState::~ParserState()
{
}

bool ParserState::ParseFile(const char* filename, std::FILE* fp)
{
  m_current_filename = filename;
  yyin = fp;
  return (yyparse(this) == 0);
}

void ParserState::ReportError(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  fprintf(stderr, "error: %s\n", msg.c_str());
}

void ParserState::ReportError(const AST::SourceLocation& loc, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  fprintf(stderr, "error at %s:%d.%d: %s\n", loc.filename ? loc.filename : "unknown", loc.first_line, loc.first_column,
          msg.c_str());
}

const Type* ParserState::AddType(const std::string& name, const Type* type)
{
  assert(m_types.find(name) == m_types.end());
  m_types.emplace(name, type);
  return type;
}

const Type* ParserState::GetType(const std::string& name)
{
  auto it = m_types.find(name);
  return (it != m_types.end()) ? it->second : nullptr;
}

const Type* ParserState::GetArrayType(const Type* base_type, const std::vector<int>& array_sizes)
{
  auto it = std::find_if(m_array_types.begin(), m_array_types.end(), [base_type, &array_sizes](const auto& ty) {
    return (ty.first.first == base_type && ty.first.second == array_sizes);
  });
  if (it != m_array_types.end())
    return it->second;

  Type* ty = Type::CreateArrayType(base_type, array_sizes);
  m_array_types.emplace_back(std::make_pair(base_type, array_sizes), ty);
  return ty;
}

void ParserState::CreateBuiltinTypes()
{
  auto MakeType = [this](const std::string& name, Type::BaseTypeId base_type_id) {
    Type* out_ref = Type::CreatePrimitiveType(name, base_type_id);
    m_global_lexical_scope->AddName(out_ref->GetName(), new AST::TypeReference(out_ref->GetName(), out_ref));
    m_types.emplace(out_ref->GetName(), out_ref);
    return out_ref;
  };

  m_void_type = MakeType("void", Type::BaseTypeId::Void);
  m_boolean_type = MakeType("boolean", Type::BaseTypeId::Boolean);
  m_bit_type = MakeType("bit", Type::BaseTypeId::Bit);
  m_int_type = MakeType("int", Type::BaseTypeId::Int);
  m_float_type = MakeType("float", Type::BaseTypeId::Float);

  // Error type doesn't get added to the symbol table
  m_error_type = Type::CreatePrimitiveType("<error>", Type::BaseTypeId::Error);
}

const Type* ParserState::GetErrorType()
{
  return m_error_type;
}

const Type* ParserState::GetBooleanType()
{
  return m_boolean_type;
}

const Type* ParserState::GetBitType()
{
  return m_bit_type;
}

const Type* ParserState::GetIntType()
{
  return m_int_type;
}

const Type* ParserState::GetFloatType()
{
  return m_float_type;
}

void yyerror(ParserState* state, const char* s)
{
  state->ReportError(yylloc, "%s", s);
}

char* yyasprintf(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
#ifdef WIN32
  int length = _vscprintf(fmt, ap) + 1;
#else
  int length = vsnprintf(0, 0, fmt, ap) + 1;
#endif
  assert(length > 0);
  va_start(ap, fmt);

  // TODO: Fix memory leaks here, probably with a recursive allocator.
  size_t memlength = static_cast<size_t>(length);
  char* str = reinterpret_cast<char*>(std::malloc(memlength));
#ifdef WIN32
  _vsnprintf_s(str, memlength, _TRUNCATE, fmt, ap);
#else
  vsnprintf(str, memlength, fmt, ap);
#endif

  va_end(ap);
  return str;
}
