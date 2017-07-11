// clang-format off
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
  global_lexical_scope = std::make_unique<AST::LexicalScope>(nullptr);
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
