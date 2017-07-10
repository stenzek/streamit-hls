#pragma once

#include <cstdlib>
#include <string>

namespace ScannerHelpers
{

// Logs an error encountered by the scanner (lexer) to stderr.
void ReportScannerError(int line, const char* fmt, ...);

char* AllocateSubStringCopy(const char* str, size_t start, size_t count);
char* AppendStrings(const char* str1, const char* str2);

int ParseIntegerLiteral(const char* str, int base);
double ParseDecimalLiteral(const char* str);

int yyparse();
} // namespace ScannerHelpers
