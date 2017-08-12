#include <cstdarg>
#include "common/log.h"

// TODO: Move this elsewhere
#if defined(_WIN32) || defined(__CYGWIN__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" void streamit_debug_print(const char* msg)
{
  Log::Debug("debug", "%s", msg);
}

extern "C" void streamit_debug_printf(const char* fmt, ...)
{
  std::va_list ap;
  va_start(ap, fmt);
  Log::Writev("debug", __FUNCTION__, LOGLEVEL_DEV, fmt, ap);
  va_end(ap);
}
