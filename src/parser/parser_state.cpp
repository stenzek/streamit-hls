// clang-format off
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include "parser/ast.h"
#include "parser/parser.h"
#include "parser/parser_state.h"
// clang-format on

ParserState::ParserState()
{
}

ParserState::~ParserState()
{
}

void ParserState::ReportError(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

void yyerror(ParserState* state, const char* s)
{
  fprintf(stderr, "input(%d): parser error: %s\n", yylloc.first_line, s);
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
