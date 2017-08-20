#include "common/string_helpers.h"
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

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

std::string HexDumpString(const void* buf, size_t len)
{
  std::stringstream ss;
  ss << "     | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F | 0123456789ABCDEF\n"
     << "-------------------------------------------------------------------------\n";

  for (size_t offs = 0; offs < len; offs += 16)
  {
    ss << std::hex << std::setfill('0') << std::setw(4) << offs << " | ";
    size_t byte_offs;
    for (byte_offs = 0; byte_offs < 16; byte_offs++)
    {
      if ((offs + byte_offs) >= len)
        break;

      ss << std::hex << std::setfill('0') << std::setw(2)
         << unsigned(reinterpret_cast<const unsigned char*>(buf)[offs + byte_offs]) << " ";
    }
    for (; byte_offs < 16; byte_offs++)
      ss << "   ";
    ss << "| ";
    for (byte_offs = 0; byte_offs < 16; byte_offs++)
    {
      if ((offs + byte_offs) >= len)
        break;
      char ch = reinterpret_cast<const char*>(buf)[offs + byte_offs];
      if (std::isprint(ch))
        ss << ch;
      else
        ss << '.';
    }
    for (; byte_offs < 16; byte_offs++)
      ss << " ";
    ss << "\n";
  }

  return ss.str();
}
