#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdint>
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
static size_t s_benchmark_bytes_read = 0;
static size_t s_benchmark_bytes_written = 0;
static std::chrono::time_point<std::chrono::steady_clock> s_last_benchmark_time;
static FILE* s_input_file = nullptr;
static FILE* s_output_file = nullptr;
static uint64_t s_benchmark_input_counter = 0;

static bool InBenchmarkMode()
{
  if (s_benchmark_mode)
    return true;

  const char* benchmark_mode_str = std::getenv("STREAMIT_BENCHMARK_MODE");
  if (benchmark_mode_str && std::atoi(benchmark_mode_str) == 1)
    s_benchmark_mode = true;

  return s_benchmark_mode;
}

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
  if (InBenchmarkMode())
    return;

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
  if (InBenchmarkMode())
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

static void UpdateBenchmarkStats()
{
  constexpr size_t BUFFER_SIZE = (100 * 1024 * 1024);
  if (s_benchmark_bytes_read < BUFFER_SIZE && s_benchmark_bytes_written < BUFFER_SIZE)
    return;

  auto current_time = std::chrono::steady_clock::now();
  std::chrono::duration<double> diff = current_time - s_last_benchmark_time;
  double input_speed = double(s_benchmark_bytes_read) / diff.count();
  double output_speed = double(s_benchmark_bytes_written) / diff.count();
  s_last_benchmark_time = current_time;
  s_benchmark_bytes_read = 0;
  s_benchmark_bytes_written = 0;
  fprintf(stderr, "Speed: Input %.4f MB/s, Output %.4f MB/s\n", input_speed / (1024.0 * 1024.0),
          output_speed / (1024.0 * 1024.0));
}

extern "C" EXPORT void streamit_read_input_file_int(void* ptr, unsigned num_bytes, unsigned count)
{
  if (s_benchmark_mode)
  {
    char* out_ptr = reinterpret_cast<char*>(ptr);
    for (unsigned i = 0; i < count; i++)
    {
      // This will simply wrap around the lower bytes if they are all that's used.
      s_benchmark_input_counter++;
      switch (num_bytes)
      {
      case 1:
        std::memcpy(ptr, &s_benchmark_input_counter, 1);
        break;
      case 4:
        std::memcpy(ptr, &s_benchmark_input_counter, 4);
        break;
      case 8:
        std::memcpy(ptr, &s_benchmark_input_counter, 8);
        break;
      default:
        std::memcpy(ptr, &s_benchmark_input_counter, num_bytes);
        break;
      }
    }

    s_benchmark_bytes_read += size_t(num_bytes) * size_t(count);
    UpdateBenchmarkStats();
    return;
  }

  std::fread(ptr, num_bytes, count, s_input_file);
}

extern "C" EXPORT void streamit_read_input_file_float(void* ptr, unsigned num_bytes, unsigned count)
{
  if (s_benchmark_mode)
  {
    char* out_ptr = reinterpret_cast<char*>(ptr);
    for (unsigned i = 0; i < count; i++)
    {
      // This will simply wrap around the lower bytes if they are all that's used.
      float val = static_cast<float>(s_benchmark_input_counter++);
      switch (num_bytes)
      {
      case 4:
        std::memcpy(ptr, &val, 4);
        break;
      case 8:
        std::memcpy(ptr, &val, 8);
        break;
      default:
        std::memcpy(ptr, &val, num_bytes);
        break;
      }
    }

    s_benchmark_bytes_read += size_t(num_bytes) * size_t(count);
    UpdateBenchmarkStats();
    return;
  }

  std::fread(ptr, num_bytes, count, s_input_file);
}

static void streamit_write_output_file(const void* ptr, unsigned num_bytes, unsigned count)
{
  if (s_benchmark_mode)
  {
    s_benchmark_bytes_written += size_t(num_bytes) * size_t(count);
    UpdateBenchmarkStats();
    return;
  }

  std::fwrite(ptr, num_bytes, count, s_output_file);
}

extern "C" EXPORT void streamit_write_output_file_int(const void* ptr, unsigned num_bytes, unsigned count)
{
  streamit_write_output_file(ptr, num_bytes, count);
}

extern "C" EXPORT void streamit_write_output_file_float(const void* ptr, unsigned num_bytes, unsigned count)
{
  streamit_write_output_file(ptr, num_bytes, count);
}
