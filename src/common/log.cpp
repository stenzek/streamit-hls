#include "common/log.h"
#include "common/string_helpers.h"
#include "spdlog/spdlog.h"

void Log::Error(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  spdlog::get(channel)->error(msg);
}

void Log::Warning(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  spdlog::get(channel)->warn(msg);
}

void Log::Info(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  spdlog::get(channel)->info(msg);
}

void Log::Debug(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  spdlog::get(channel)->debug(msg);
}
