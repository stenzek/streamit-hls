#include "common/log.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "common/string_helpers.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace Log
{
struct RegisteredCallback
{
  CallbackFunctionType Function;
  void* Parameter;
};

typedef std::vector<RegisteredCallback> RegisteredCallbackList;
RegisteredCallbackList m_callbacks;
std::mutex m_callbackLock;

LOGLEVEL m_filterLevel = LOGLEVEL_TRACE;

static std::chrono::high_resolution_clock::time_point s_startTimeStamp = std::chrono::high_resolution_clock::now();

void RegisterCallback(CallbackFunctionType callbackFunction, void* pUserParam)
{
  RegisteredCallback Callback;
  Callback.Function = callbackFunction;
  Callback.Parameter = pUserParam;

  std::lock_guard<std::mutex> guard(m_callbackLock);
  m_callbacks.push_back(Callback);
}

void UnregisterCallback(CallbackFunctionType callbackFunction, void* pUserParam)
{
  std::lock_guard<std::mutex> guard(m_callbackLock);

  for (RegisteredCallbackList::iterator iter = m_callbacks.begin(); iter != m_callbacks.end(); iter++)
  {
    if (iter->Function == callbackFunction && iter->Parameter == pUserParam)
    {
      m_callbacks.erase(iter);
      break;
    }
  }
}

void FormatLogMessageForDisplay(const char* channelName, const char* functionName, LOGLEVEL level, const char* message,
                                void (*printCallback)(const char*, void*), void* pCallbackUserData)
{
  static const char levelCharacters[LOGLEVEL_COUNT] = {'X', 'E', 'W', 'P', 'S', 'I', 'D', 'R', 'T'};

  // find time since start of process
  auto time_diff = (std::chrono::high_resolution_clock::now() - s_startTimeStamp);
  float message_time = std::chrono::duration<float>(time_diff).count();

// write prefix
#ifndef Y_BUILD_CONFIG_SHIPPING
  char prefix[256];
  if (level <= LOGLEVEL_PERF)
    snprintf(prefix, sizeof(prefix), "[%10.4f] %c(%s): ", message_time, levelCharacters[level], functionName);
  else
    snprintf(prefix, sizeof(prefix), "[%10.4f] %c/%s: ", message_time, levelCharacters[level], channelName);

  printCallback(prefix, pCallbackUserData);
#else
  char prefix[256];
  snprintf(prefix, sizeof(prefix), "[%10.4f] %c/%s: ", message_time, levelCharacters[level], channelName);
  printCallback(prefix, pCallbackUserData);
#endif

  // write message
  printCallback(message, pCallbackUserData);
}

static bool s_consoleOutputEnabled = false;
static std::string s_consoleOutputChannelFilter;
static LOGLEVEL s_consoleOutputLevelFilter = LOGLEVEL_TRACE;

static bool s_debugOutputEnabled = false;
static std::string s_debugOutputChannelFilter;
static LOGLEVEL s_debugOutputLevelFilter = LOGLEVEL_TRACE;

#if defined(_WIN32)

static void ConsoleOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName,
                                     LOGLEVEL level, const char* message)
{
  if (!s_consoleOutputEnabled || level > s_consoleOutputLevelFilter ||
      s_consoleOutputChannelFilter.Find(channelName) >= 0)
    return;

  if (level > LOGLEVEL_COUNT)
    level = LOGLEVEL_TRACE;

  HANDLE hConsole = GetStdHandle((level <= LOGLEVEL_WARNING) ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  if (hConsole != INVALID_HANDLE_VALUE)
  {
    static const WORD levelColors[LOGLEVEL_COUNT] = {
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN,                        // NONE
      FOREGROUND_RED | FOREGROUND_INTENSITY,                                      // ERROR
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,                   // WARNING
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,                    // PERF
      FOREGROUND_GREEN | FOREGROUND_INTENSITY,                                    // SUCCESS
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // INFO
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN,                        // DEV
      FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY,                  // PROFILE
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN,                        // TRACE
    };

    CONSOLE_SCREEN_BUFFER_INFO oldConsoleScreenBufferInfo;
    GetConsoleScreenBufferInfo(hConsole, &oldConsoleScreenBufferInfo);
    SetConsoleTextAttribute(hConsole, levelColors[level]);

    // write message in the formatted way
    Log::FormatLogMessageForDisplay(channelName, functionName, level, message,
                                    [](const char* text, void* hConsole) {
                                      DWORD written;
                                      WriteConsoleA((HANDLE)hConsole, text, Y_strlen(text), &written, nullptr);
                                    },
                                    (void*)hConsole);

    // write newline
    DWORD written;
    WriteConsoleA(hConsole, "\r\n", 2, &written, nullptr);

    // restore color
    SetConsoleTextAttribute(hConsole, oldConsoleScreenBufferInfo.wAttributes);
  }
}

static void DebugOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName, LOGLEVEL level,
                                   const char* message)
{
  if (!s_debugOutputEnabled || level > s_debugOutputLevelFilter || s_debugOutputChannelFilter.Find(channelName) >= 0)
    return;

  Log::FormatLogMessageForDisplay(channelName, functionName, level, message,
                                  [](const char* text, void*) { OutputDebugStringA(text); });

  OutputDebugStringA("\n");
}

#else

static void ConsoleOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName,
                                     LOGLEVEL level, const char* message)
{
  if (!s_consoleOutputEnabled || level > s_consoleOutputLevelFilter ||
      s_consoleOutputChannelFilter.find(channelName) != std::string::npos)
    return;

  static const char* colorCodes[LOGLEVEL_COUNT] = {
    "\033[0m",    // NONE
    "\033[1;31m", // ERROR
    "\033[1;33m", // WARNING
    "\033[1;35m", // PERF
    "\033[1;37m", // SUCCESS
    "\033[1;37m", // INFO
    "\033[0;37m", // DEV
    "\033[1;36m", // PROFILE
    "\033[1;36m", // TRACE
  };

  int outputFd = (level <= LOGLEVEL_WARNING) ? STDERR_FILENO : STDOUT_FILENO;

  write(outputFd, colorCodes[level], std::strlen(colorCodes[level]));

  Log::FormatLogMessageForDisplay(
    channelName, functionName, level, message,
    [](const char* text, void* outputFd) { write((int)(intptr_t)outputFd, text, std::strlen(text)); },
    (void*)(intptr_t)outputFd);

  write(outputFd, colorCodes[0], std::strlen(colorCodes[0]));
  write(outputFd, "\n", 1);
}

static void DebugOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName, LOGLEVEL level,
                                   const char* message)
{
}

#endif

void SetConsoleOutputParams(bool Enabled, const char* ChannelFilter, LOGLEVEL LevelFilter)
{
  if (s_consoleOutputEnabled != Enabled)
  {
    s_consoleOutputEnabled = Enabled;
    if (Enabled)
      RegisterCallback(ConsoleOutputLogCallback, NULL);
    else
      UnregisterCallback(ConsoleOutputLogCallback, NULL);

#if defined(Y_PLATFORM_WINDOWS)
    // On windows, no console is allocated by default on a windows based application
    static bool consoleWasAllocated = false;
    if (Enabled)
    {
      if (GetConsoleWindow() == NULL)
      {
        assert(!consoleWasAllocated);
        consoleWasAllocated = true;
        AllocConsole();

        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
      }
    }
    else
    {
      if (consoleWasAllocated)
      {
        FreeConsole();
        consoleWasAllocated = false;
      }
    }
#endif
  }

  s_consoleOutputChannelFilter = (ChannelFilter != NULL) ? ChannelFilter : "";
  s_consoleOutputLevelFilter = LevelFilter;
}

void SetDebugOutputParams(bool enabled, const char* channelFilter /* = nullptr */,
                          LOGLEVEL levelFilter /* = LOGLEVEL_TRACE */)
{
  if (s_debugOutputEnabled != enabled)
  {
    s_debugOutputEnabled = enabled;
    if (enabled)
      RegisterCallback(DebugOutputLogCallback, nullptr);
    else
      UnregisterCallback(DebugOutputLogCallback, nullptr);
  }

  s_debugOutputChannelFilter = (channelFilter != nullptr) ? channelFilter : "";
  s_debugOutputLevelFilter = levelFilter;
}

void SetFilterLevel(LOGLEVEL level)
{
  assert(level < LOGLEVEL_COUNT);
  m_filterLevel = level;
}

void ExecuteCallbacks(const char* channelName, const char* functionName, LOGLEVEL level, const char* message)
{
  std::lock_guard<std::mutex> guard(m_callbackLock);
  for (RegisteredCallback& callback : m_callbacks)
    callback.Function(callback.Parameter, channelName, functionName, level, message);
}

void Write(const char* channelName, const char* functionName, LOGLEVEL level, const char* message)
{
  if (level > m_filterLevel)
    return;

  ExecuteCallbacks(channelName, functionName, level, message);
}

void Writef(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, ...)
{
  if (level > m_filterLevel)
    return;

  va_list ap;
  va_start(ap, format);
  Writev(channelName, functionName, level, format, ap);
  va_end(ap);
}

void Writev(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, std::va_list ap)
{
  if (level > m_filterLevel)
    return;

  std::string temp = StringFromFormatV(format, ap);
  Write(channelName, functionName, level, temp.c_str());
}

void Error(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  Writev(channel, channel, LOGLEVEL_ERROR, fmt, ap);
  va_end(ap);
}

void Warning(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  Writev(channel, channel, LOGLEVEL_ERROR, fmt, ap);
  va_end(ap);
}

void Info(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  Writev(channel, channel, LOGLEVEL_INFO, fmt, ap);
  va_end(ap);
}

void Debug(const char* channel, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  Writev(channel, channel, LOGLEVEL_DEV, fmt, ap);
  va_end(ap);
}

} // namespace Log