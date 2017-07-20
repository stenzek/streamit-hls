#pragma once
#include <cstdarg>
#include <string>

std::string StringFromFormat(const char* fmt, ...);
std::string StringFromFormatV(const char* fmt, va_list ap);
