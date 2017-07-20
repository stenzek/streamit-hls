#include "common/string_helpers.h"
#include <cstdio>

std::string StringFromFormat(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string ret = StringFromFormatV(fmt, ap);
  va_end(ap);
  return ret;
}

std::string StringFromFormatV(const char* fmt, va_list ap)
{
  std::va_list ap2;
  va_copy(ap2, ap);

#ifdef WIN32
  int length = _vscprintf(fmt, ap2);
#else
  int length = vsnprintf(0, 0, fmt, ap2);
#endif
  va_end(ap2);

  if (length <= 0)
    return {};

  std::string str;
  str.resize(static_cast<size_t>(length));
#ifdef WIN32
  _vsnprintf_s(&str[0], length + 1, _TRUNCATE, fmt, ap);
#else
  vsnprintf(&str[0], length + 1, fmt, ap);
#endif

  return str;
}
