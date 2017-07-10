#include "parser/ast_printer.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#ifdef DEBUG
#include <iostream>
#endif

static std::string StringFromFormat(const char* fmt, va_list ap)
{
  va_list tmp_ap;
  va_copy(tmp_ap, ap);
#ifdef WIN32
  int length = _vscprintf(fmt, tmp_ap) + 1;
#else
  int length = vsnprintf(0, 0, fmt, tmp_ap) + 1;
#endif
  assert(length > 0);

  if (length == 0)
    return std::string();

  size_t memlength = static_cast<size_t>(length);
  char* temp = new char[memlength];

#ifdef WIN32
  _vsnprintf_s(temp, memlength, _TRUNCATE, fmt, ap);
#else
  vsnprintf(temp, memlength, fmt, ap);
#endif

  va_end(tmp_ap);

  std::string ret(temp);
  delete[] temp;
  return ret;
}

static std::string IndentString(int indent)
{
  std::stringstream ss;
  for (int i = 0; i < indent; i++)
    ss << "  ";
  return ss.str();
}

void ASTPrinter::BeginBlock(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  m_ss << IndentString(m_indent) << StringFromFormat(fmt, ap) << ": {" << std::endl;
  va_end(ap);
  m_indent++;
#ifdef DEBUG
  std::cout << m_ss.str() << std::endl;
#endif
}

void ASTPrinter::Write(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  m_ss << IndentString(m_indent) << StringFromFormat(fmt, ap);
  va_end(ap);
}

void ASTPrinter::WriteLine(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  m_ss << IndentString(m_indent) << StringFromFormat(fmt, ap) << std::endl;
  va_end(ap);
}

void ASTPrinter::EndBlock()
{
  assert(m_indent > 0);
  m_indent--;
  m_ss << IndentString(m_indent) << "}" << std::endl;
}
