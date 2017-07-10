#pragma once
#include <sstream>

class ASTPrinter
{
public:
  ASTPrinter() = default;
  ~ASTPrinter() = default;

  std::string ToString() const
  {
    return m_ss.str();
  }

  void BeginBlock(const char* fmt, ...);
  void Write(const char* fmt, ...);
  void WriteLine(const char* fmt, ...);
  void EndBlock();

private:
  std::stringstream m_ss;
  int m_indent = 0;
};
