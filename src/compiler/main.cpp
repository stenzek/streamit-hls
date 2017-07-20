#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"

extern bool temp_codegenerator_run(ParserState*);

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

  state.DumpAST();

  if (!temp_codegenerator_run(&state))
  {
    std::cout << "Generating code failed." << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
