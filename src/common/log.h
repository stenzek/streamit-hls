#pragma once
#include <cstdarg>

enum LOGLEVEL
{
  LOGLEVEL_NONE = 0,    // Silences all log traffic
  LOGLEVEL_ERROR = 1,   // "ErrorPrint"
  LOGLEVEL_WARNING = 2, // "WarningPrint"
  LOGLEVEL_PERF = 3,    // "PerfPrint"
  LOGLEVEL_SUCCESS = 4, // "SuccessPrint"
  LOGLEVEL_INFO = 5,    // "InfoPrint"
  LOGLEVEL_DEV = 6,     // "DevPrint"
  LOGLEVEL_PROFILE = 7, // "ProfilePrint"
  LOGLEVEL_TRACE = 8,   // "TracePrint"
  LOGLEVEL_COUNT = 9,
};

namespace Log
{

// log message callback type
typedef void (*CallbackFunctionType)(void* pUserParam, const char* channelName, const char* functionName,
                                     LOGLEVEL level, const char* message);

// registers a log callback
void RegisterCallback(CallbackFunctionType callbackFunction, void* pUserParam);

// unregisters a log callback
void UnregisterCallback(CallbackFunctionType callbackFunction, void* pUserParam);

// adds a standard console output
void SetConsoleOutputParams(bool enabled, const char* channelFilter = nullptr, LOGLEVEL levelFilter = LOGLEVEL_TRACE);

// adds a debug console output [win32/android only]
void SetDebugOutputParams(bool enabled, const char* channelFilter = nullptr, LOGLEVEL levelFilter = LOGLEVEL_TRACE);

// Sets global filtering level, messages below this level won't be sent to any of the logging sinks.
void SetFilterLevel(LOGLEVEL level);

// writes a message to the log
void Write(const char* channelName, const char* functionName, LOGLEVEL level, const char* message);
void Writef(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, ...);
void Writev(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, std::va_list ap);

// formats a log message for display, involves multiple print calls
static void FormatLogMessageForDisplay(const char* channelName, const char* functionName, LOGLEVEL level,
                                       const char* message, void (*printCallback)(const char*, void*),
                                       void* pCallbackUserData = nullptr);

void Error(const char* channel, const char* fmt, ...);
void Warning(const char* channel, const char* fmt, ...);
void Info(const char* channel, const char* fmt, ...);
void Debug(const char* channel, const char* fmt, ...);
}

#ifdef Y_BUILD_CONFIG_SHIPPING
#define LOG_MESSAGE_FUNCTION_NAME ""
#else
#define LOG_MESSAGE_FUNCTION_NAME __FUNCTION__
#endif

// log wrappers
#define Log_SetChannel(ChannelName) static const char* ___LogChannel___ = #ChannelName;
#define Log_ErrorPrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_ERROR, msg)
#define Log_ErrorPrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_ERROR, __VA_ARGS__)
#define Log_WarningPrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_WARNING, msg)
#define Log_WarningPrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_WARNING, __VA_ARGS__)
#define Log_SuccessPrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_SUCCESS, msg)
#define Log_SuccessPrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_SUCCESS, __VA_ARGS__)
#define Log_InfoPrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_INFO, msg)
#define Log_InfoPrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_INFO, __VA_ARGS__)
#define Log_PerfPrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_PERF, msg)
#define Log_PerfPrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_PERF, __VA_ARGS__)
#define Log_DevPrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_DEV, msg)
#define Log_DevPrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_DEV, __VA_ARGS__)
#define Log_ProfilePrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_PROFILE, msg)
#define Log_ProfilePrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_PROFILE, __VA_ARGS__)

#ifdef Y_BUILD_CONFIG_RELEASE
#define Log_TracePrint(msg)                                                                                            \
  do                                                                                                                   \
  {                                                                                                                    \
  } while (0)
#define Log_TracePrintf(...)                                                                                           \
  do                                                                                                                   \
  {                                                                                                                    \
  } while (0)
#else
#define Log_TracePrint(msg) Log::Write(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_TRACE, msg)
#define Log_TracePrintf(...) Log::Writef(___LogChannel___, LOG_MESSAGE_FUNCTION_NAME, LOGLEVEL_TRACE, __VA_ARGS__)
#endif
