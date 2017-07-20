// clang-format off
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "parser/ast.h"
#include "parser/ast_printer.h"
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

  int res = yyparse(this);
  if (res != 0)
  {
    ReportError("yyparse() returned %d", res);
    return false;
  }

  if (!SemanticAnalysis())
  {
    ReportError("Semantic analysis failed.");
    return false;
  }

  return true;
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

void ParserState::AddFilter(AST::FilterDeclaration* decl)
{
  m_filters.push_back(decl);
}

void ParserState::AddStream(AST::StreamDeclaration* decl)
{
  m_streams.push_back(decl);
}

void ParserState::AddActiveStream(AST::Node* stream)
{
  assert(!HasActiveStream(stream));
  m_active_streams.push_back(stream);
}

bool ParserState::HasActiveStream(AST::Node* stream)
{
  return std::any_of(m_active_streams.begin(), m_active_streams.end(),
                     [stream](AST::Node* s) { return (stream == s); });
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

bool ParserState::SemanticAnalysis()
{
  bool result = true;

  // Add everything to the symbol table first, since filters can be defined after they are referenced.
  for (auto* filter : m_filters)
  {
    if (!m_global_lexical_scope->AddName(filter->GetName(), filter))
    {
      ReportError(filter->GetSourceLocation(), "Duplicate filter declaration '%s'", filter->GetName().c_str());
      result = false;
    }
  }
  for (auto* stream : m_streams)
  {
    if (!m_global_lexical_scope->AddName(stream->GetName(), stream))
    {
      ReportError(stream->GetSourceLocation(), "Duplicate stream declaration '%s'", stream->GetName().c_str());
      result = false;
    }
  }

  // Now perform semantic analysis.
  for (auto* filter : m_filters)
  {
    if (!HasActiveStream(filter))
    {
      AddActiveStream(filter);
      result &= filter->SemanticAnalysis(this, m_global_lexical_scope.get());
    }
  }
  for (auto* stream : m_streams)
  {
    if (!HasActiveStream(stream))
    {
      AddActiveStream(stream);
      result &= stream->SemanticAnalysis(this, m_global_lexical_scope.get());
    }
  }

  return result;
}

void ParserState::DumpAST()
{
  ASTPrinter printer;
  printer.BeginBlock("Filters");
  {
    unsigned int counter = 0;
    for (auto* filter : m_filters)
    {
      printer.Write("Filter[%u]: ", counter++);
      filter->Dump(&printer);
    }
  }
  printer.EndBlock();

  printer.BeginBlock("Streams");
  {
    unsigned int counter = 0;
    for (auto* stream : m_streams)
    {
      printer.Write("Pipeline[%u]: ", counter++);
      stream->Dump(&printer);
    }
  }
  printer.EndBlock();

  std::cout << "Dumping AST: " << std::endl;
  std::cout << printer.ToString() << std::endl;
  std::cout << "End of AST." << std::endl;

  std::cout << "Dumping global symbol table: " << std::endl;
  for (const auto& it : *m_global_lexical_scope)
    std::cout << "  " << it.first << std::endl;
  std::cout << "End of global symbol table." << std::endl;
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
