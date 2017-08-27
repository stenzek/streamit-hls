// clang-format off
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "common/log.h"
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_defines.h"
#include "parser/parser_tokens.h"
#include "parser/parser_state.h"
// clang-format on

extern FILE* yyin;
extern int yydebug;
int yyparse(ParserState* state);

ParserState::ParserState()
{
  m_global_lexical_scope = std::make_unique<AST::LexicalScope>(nullptr);
  CreateBuiltinTypes();
  CreateBuiltinFunctions();
}

ParserState::~ParserState()
{
}

bool ParserState::ParseFile(const char* filename, std::FILE* fp, bool debug)
{
  m_current_filename = filename;
  yyin = fp;
  yydebug = debug ? 1 : 0;

  // Auto set entry point based on file name
  if (m_entry_point_name.empty())
    AutoSetEntryPoint(filename);

  int res = yyparse(this);
  if (res != 0)
  {
    LogError("yyparse() returned %d", res);
    return false;
  }

  if (!SemanticAnalysis())
  {
    LogError("Semantic analysis failed.");
    return false;
  }

  return true;
}

bool ParserState::AutoSetEntryPoint(const char* filename)
{
  const char* start = std::strrchr(filename, '/');
  if (!start)
  {
    start = std::strrchr(filename, '\\');
    if (!start)
      start = filename;
    else
      start++;
  }
  else
  {
    start++;
  }

  if (std::strlen(start) == 0)
    return false;

  const char* end = std::strrchr(start, '.');
  if (!end)
    end = start + std::strlen(start) - 1;

  if (start == end)
    return false;

  m_entry_point_name.clear();
  m_entry_point_name.append(start, end - start);
  LogInfo("Automatic entry point based on filename: %s", m_entry_point_name.c_str());
  return true;
}

void ParserState::LogError(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  Log::Error("parser", "%s", msg.c_str());
}

void ParserState::LogWarning(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  Log::Warning("parser", "%s", msg.c_str());
}

void ParserState::LogInfo(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  Log::Info("parser", "%s", msg.c_str());
}

void ParserState::LogDebug(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  Log::Debug("parser", "%s", msg.c_str());
}

void ParserState::LogError(const AST::SourceLocation& loc, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  Log::Error("parser", "%s:%d.%d: %s", loc.filename ? loc.filename : "unknown", loc.first_line, loc.first_column,
             msg.c_str());
}

void ParserState::LogWarning(const AST::SourceLocation& loc, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);

  Log::Warning("parser", "%s:%d.%d: %s", loc.filename ? loc.filename : "unknown", loc.first_line, loc.first_column,
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

  Type* ty = ArrayType::Create(base_type, array_sizes);
  m_array_types.emplace_back(std::make_pair(base_type, array_sizes), ty);
  return ty;
}

void ParserState::CreateBuiltinTypes()
{
  auto MakeType = [this](Type::TypeId base_type_id) {
    Type* out_ref = Type::CreatePrimitive(base_type_id);
    m_global_lexical_scope->AddName(out_ref->GetName(), new AST::TypeReference(out_ref->GetName(), out_ref));
    m_types.emplace(out_ref->GetName(), out_ref);
    return out_ref;
  };

  m_void_type = MakeType(Type::TypeId::Void);
  m_boolean_type = MakeType(Type::TypeId::Boolean);
  m_bit_type = MakeType(Type::TypeId::Bit);
  m_int_type = MakeType(Type::TypeId::Int);
  m_float_type = MakeType(Type::TypeId::Float);

  // Error type doesn't get added to the symbol table
  m_error_type = Type::CreatePrimitive(Type::TypeId::Error);
}

void ParserState::CreateBuiltinFunctions()
{
  auto MakeExternalFunction = [this](const char* name, const Type* return_type,
                                     const std::vector<const Type*>& arg_types) {
    AST::FunctionReference* fref = new AST::FunctionReference(name, return_type, arg_types, true);
    m_global_lexical_scope->AddName(fref->GetSymbolName(), fref);
  };

  MakeExternalFunction("println", m_void_type, {m_int_type});
}

bool ParserState::SemanticAnalysis()
{
  bool result = true;

  // Add everything to the symbol table first, since filters can be defined after they are referenced.
  for (auto* filter : m_filters)
  {
    if (!m_global_lexical_scope->AddName(filter->GetName(), filter))
    {
      LogError(filter->GetSourceLocation(), "Duplicate filter declaration '%s'", filter->GetName().c_str());
      result = false;
    }
  }
  for (auto* stream : m_streams)
  {
    if (!m_global_lexical_scope->AddName(stream->GetName(), stream))
    {
      LogError(stream->GetSourceLocation(), "Duplicate stream declaration '%s'", stream->GetName().c_str());
      result = false;
    }
  }

  // Now perform semantic analysis.
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

  LogInfo("Dumping AST: ");
  std::cout << printer.ToString() << std::endl;
  LogInfo("End of AST.");

  LogInfo("Dumping global symbol table: ");
  for (const auto& it : *m_global_lexical_scope)
    LogInfo("  %s", it.first.c_str());
  LogInfo("End of global symbol table.");

  LogInfo("Entry point: %s", m_entry_point_name.c_str());
}

const Type* ParserState::GetResultType(const Type* lhs, const Type* rhs)
{
  // same type -> same type
  if (lhs == rhs)
    return lhs;

  // int + float -> float
  if ((lhs->IsInt() || rhs->IsInt()) && (lhs->IsFloat() || rhs->IsFloat()))
    return GetFloatType();

  return GetErrorType();
}

const Type* ParserState::GetArrayElementType(const ArrayType* ty)
{
  if (ty->GetArraySizes().empty())
    return GetErrorType();

  // Last array index, or single-dimension array
  if (ty->GetArraySizes().size() == 1)
    return ty->GetBaseType();

  // Drop one of the arrays off, so int[10][11][12] -> int[10][11].
  std::vector<int> temp = ty->GetArraySizes();
  temp.pop_back();
  return GetArrayType(ty->GetBaseType(), temp);
}

void yyerror(ParserState* state, const char* s)
{
  state->LogError(yylloc, "%s", s);
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
