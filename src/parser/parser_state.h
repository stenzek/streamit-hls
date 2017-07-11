#pragma once
#include <memory>
#include "parser/ast.h"

class ParserState
{
public:
  ParserState();
  ~ParserState();

  void ReportError(const char* fmt, ...);

  // TODO: Move this to private
  AST::Program* program = nullptr;

  // Global lexical scope
  std::unique_ptr<AST::LexicalScope> global_lexical_scope;

  // Filters can't be nested? So this should be sufficient.
  // TODO: Kinda messy though. Maybe would be better placed in the symbol table.
  AST::FilterDeclaration* current_filter = nullptr;
};

void yyerror(ParserState* state, const char* s);
char* yyasprintf(const char* fmt, ...);
int yyparse(ParserState* state);
