#include "parser/scanner_helpers.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace ScannerHelpers
{

void ReportScannerError(int line, const char* fmt, ...)
{
  // TODO: Report file name once we track it.
  va_list ap;
  va_start(ap, fmt);
  std::fprintf(stderr, "input(%d): scanner error: ", line);
  std::vfprintf(stderr, fmt, ap);
  std::fprintf(stderr, "\n");
  va_end(ap);
}

char* AllocateSubStringCopy(const char* str, size_t start, size_t count)
{
  // Check start as well as start+count in case of integer overflow.
  size_t len = strlen(str);
  assert((start + count) >= start && (start + count) <= len);

  char* temp = reinterpret_cast<char*>(std::malloc(count + 1));
  if (count > 0)
    std::memcpy(temp, str + start, count);
  temp[count] = 0;
  return temp;
}

char* AppendStrings(const char* str1, const char* str2)
{
  size_t len1 = strlen(str1);
  size_t len2 = strlen(str2);

  char* temp = reinterpret_cast<char*>(std::malloc(len1 + len2 + 1));
  std::memcpy(temp, str1, len1);
  std::memcpy(temp + len1, str2, len2);
  temp[len1 + len2] = 0;
  return temp;
}

int ParseIntegerLiteral(const char* str, int base)
{
  return static_cast<int>(std::strtol(str, nullptr, base));
}

double ParseDecimalLiteral(const char* str)
{
  return std::atof(str);
}

} // namespace ScannerHelpers
