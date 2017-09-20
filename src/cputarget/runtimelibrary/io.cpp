#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// TODO: Move this elsewhere
#if defined(_WIN32) || defined(__CYGWIN__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

static std::string s_input_file_name;
static std::string s_output_file_name;
static bool s_benchmark_mode = false;
static size_t s_benchmark_bytes_written = 0;
static std::chrono::time_point<std::chrono::steady_clock> s_last_benchmark_time;
static FILE* s_input_file = nullptr;
static FILE* s_output_file = nullptr;

extern "C" EXPORT void streamit_set_input_file_name(const char* name)
{
  s_input_file_name = name;
}

extern "C" EXPORT void streamit_set_output_file_name(const char* name)
{
  s_output_file_name = name;
}

extern "C" EXPORT void streamit_set_benchmark_mode()
{
  s_benchmark_mode = true;
}

extern "C" EXPORT void streamit_open_input_file(const char* filename)
{
  // prioritize user-specified input file
  std::string actual_filename;
  if (!s_input_file_name.empty())
    actual_filename = s_input_file_name;
  else if (filename && std::strlen(filename) > 0)
    actual_filename = filename;
  else
    actual_filename = "input.stream";

  assert(!s_input_file);
  s_input_file = std::fopen(actual_filename.c_str(), "rb");
  if (!s_input_file)
  {
    fprintf(stderr, "Failed to open input file '%s'\n", actual_filename.c_str());
    std::quick_exit(-1);
  }
}

extern "C" EXPORT void streamit_open_output_file(const char* filename)
{
  const char* benchmark_mode_str = std::getenv("STREAMIT_BENCHMARK_MODE");
  if (benchmark_mode_str && std::atoi(benchmark_mode_str) == 1)
    s_benchmark_mode = true;

  if (s_benchmark_mode)
  {
    s_last_benchmark_time = std::chrono::steady_clock::now();
    return;
  }

  // prioritize user-specified input file
  std::string actual_filename;
  if (!s_output_file_name.empty())
    actual_filename = s_output_file_name;
  else if (filename && std::strlen(filename) > 0)
    actual_filename = filename;
  else
    actual_filename = "output.stream";

  assert(!s_output_file);
  s_output_file = std::fopen(actual_filename.c_str(), "wb");
  if (!s_output_file)
  {
    fprintf(stderr, "Failed to open output file '%s'\n", actual_filename.c_str());
    std::quick_exit(-1);
  }
}

extern "C" EXPORT void streamit_read_input_file(void* ptr, unsigned num_bytes, unsigned count)
{
  std::fread(ptr, num_bytes, count, s_input_file);
}

extern "C" EXPORT void streamit_write_output_file(const void* ptr, unsigned num_bytes, unsigned count)
{
  if (s_benchmark_mode)
  {
    s_benchmark_bytes_written += size_t(num_bytes) * size_t(count);
    if (s_benchmark_bytes_written > (100 * 1024 * 1024))
    {
      auto current_time = std::chrono::steady_clock::now();
      std::chrono::duration<double> diff = current_time - s_last_benchmark_time;
      double speed = double(s_benchmark_bytes_written) / diff.count();
      s_last_benchmark_time = current_time;
      s_benchmark_bytes_written = 0;
      fprintf(stderr, "Speed: %.4f MB/s\n", speed / (1024.0 * 1024.0));
    }

    return;
  }

  std::fwrite(ptr, num_bytes, count, s_output_file);
}
