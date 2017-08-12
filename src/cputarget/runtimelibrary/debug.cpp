#include <cstdarg>
#include <cstdio>

// TODO: Move this elsewhere
#if defined(_WIN32) || defined(__CYGWIN__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" void streamit_debug_print(const char* msg)
{
  std::fprintf(stdout, "streamit_debug_print: %s\n", msg);
}

extern "C" void streamit_debug_printf(const char* fmt, ...)
{
  std::va_list ap;
  va_start(ap, fmt);
  std::fprintf(stdout, "streamit_debug_print: ");
  std::vfprintf(stdout, fmt, ap);
  std::fprintf(stdout, "\n");
  va_end(ap);
}
