#define YYDEBUG 0

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"

extern FILE* yyin;
extern bool temp_codegenerator_run(AST::Program* program);

int main(int argc, char* argv[])
{
#if YYDEBUG
  yydebug = 1;
#endif

  if (argc > 1)
  {
    yyin = fopen(argv[1], "r");
    if (!yyin)
    {
      std::cerr << "Failed to open file " << argv[1] << std::endl;
      return EXIT_FAILURE;
    }
  }

  ParserState state;
  int exit_value = yyparse(&state);
  if (exit_value != 0)
  {
    std::cerr << "Parse failed. Exiting." << std::endl;
    return EXIT_FAILURE;
  }

  if (!state.program)
  {
    std::cerr << "No program/root node. Exiting." << std::endl;
    return EXIT_FAILURE;
  }

  AST::LexicalScope global_symbol_table(nullptr);
  if (!state.program->SemanticAnalysis(&state, &global_symbol_table))
  {
    std::cout << "Semantic analysis failed." << std::endl;
    return EXIT_FAILURE;
  }

  ASTPrinter printer;
  state.program->Dump(&printer);

  std::cout << "Dumping AST: " << std::endl;
  std::cout << printer.ToString() << std::endl;
  std::cout << "End of AST." << std::endl;

  std::cout << "Dumping global symbol table: " << std::endl;
  for (const auto& it : global_symbol_table)
    std::cout << "  " << it.first << std::endl;
  std::cout << "End of global symbol table." << std::endl;

  if (!temp_codegenerator_run(state.program))
  {
    std::cout << "Generating code failed." << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
