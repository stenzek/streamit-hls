#pragma once

namespace AST
{
class Program;
class FilterDeclaration;
}

class ParserState
{
public:
  ParserState();
  ~ParserState();

  void ReportError(const char* fmt, ...);

  // TODO: Move this to private
  AST::Program* program = nullptr;

  // Filters can't be nested? So this should be sufficient.
  // TODO: Kinda messy though. Maybe would be better placed in the symbol table.
  AST::FilterDeclaration* current_filter = nullptr;
};

void yyerror(ParserState* state, const char* s);
char* yyasprintf(const char* fmt, ...);
int yyparse(ParserState* state);
