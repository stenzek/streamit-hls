#include <cstdio>

// TODO: Move this elsewhere
#if defined(_WIN32) || defined(__CYGWIN__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" EXPORT void streamit_println___int(int arg)
{
  std::printf("%d\n", arg);
}
