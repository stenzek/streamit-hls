#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <string>

std::string StringFromFormat(const char* fmt, ...);
std::string StringFromFormatV(const char* fmt, va_list ap);
std::string HexDumpString(const void* buf, size_t len);

// TODO: Move me
template <typename T>
static T gcd(T k, T m)
{
  assert(k >= T(0) && m >= T(0));
  while (k != m)
  {
    if (k > m)
      k -= m;
    else
      m -= k;
  }
  return k;
}
