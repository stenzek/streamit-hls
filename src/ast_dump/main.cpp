#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"

int main(int argc, char* argv[])
{
  const char* filename = "stdin";
  std::FILE* fp = stdin;

  if (argc > 1)
  {
    filename = argv[1];
    fp = fopen(filename, "r");
    if (!fp)
    {
      std::cerr << "Failed to open file " << filename << std::endl;
      return EXIT_FAILURE;
    }
  }

  ParserState state;
  if (!state.ParseFile(filename, fp))
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
  if (!state.program->SemanticAnalysis(&state, state.global_lexical_scope.get()))
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

  return EXIT_SUCCESS;
}
