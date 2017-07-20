#pragma once

namespace Log
{
void Error(const char* channel, const char* fmt, ...);
void Warning(const char* channel, const char* fmt, ...);
void Info(const char* channel, const char* fmt, ...);
void Debug(const char* channel, const char* fmt, ...);
}
