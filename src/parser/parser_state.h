#pragma once
#include <cstdio>
#include <memory>
#include <string>
#include "parser/ast.h"

class ParserState
{
public:
  ParserState();
  ~ParserState();

  const std::string& GetCurrentFileName() const
  {
    return m_current_filename;
  }

  bool ParseFile(const char* filename, std::FILE* fp);

  void ReportError(const char* fmt, ...);
  void ReportError(const AST::SourceLocation& loc, const char* fmt, ...);

  // TODO: Move this to private
  AST::Program* program = nullptr;

  // Global lexical scope
  std::unique_ptr<AST::LexicalScope> global_lexical_scope;

  // Filters can't be nested? So this should be sufficient.
  // TODO: Kinda messy though. Maybe would be better placed in the symbol table.
  AST::FilterDeclaration* current_filter = nullptr;

private:
  std::string m_current_filename;
};

void yyerror(ParserState* state, const char* s);
char* yyasprintf(const char* fmt, ...);
