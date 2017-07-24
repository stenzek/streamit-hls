#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include "common/log.h"
#include "frontend/frontend.h"
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"

int main(int argc, char* argv[])
{
  const char* filename = "stdin";
  std::FILE* fp = stdin;

  Log::SetConsoleOutputParams(true);

  if (argc > 1)
  {
    filename = argv[1];
    fp = fopen(filename, "r");
    if (!fp)
    {
      Log::Error("main", "Failed to open file {}", filename);
      return EXIT_FAILURE;
    }
  }

  ParserState state;
  if (!state.ParseFile(filename, fp))
  {
    Log::Error("main", "Parse failed. Exiting.");
    return EXIT_FAILURE;
  }

  state.DumpAST();

  Frontend::Context* ctx = Frontend::CreateContext();
  if (!Frontend::GenerateStreamGraph(ctx, &state))
  {
    Log::Error("main", "Generating stream graph failed. Exiting.");
    Frontend::DestroyContext(ctx);
    return EXIT_FAILURE;
  }

  if (!Frontend::GenerateFilterFunctions(ctx, &state))
  {
    Log::Error("main", "Generating code failed. Exiting.");
    Frontend::DestroyContext(ctx);
    return EXIT_FAILURE;
  }

  Frontend::DestroyContext(ctx);
  return EXIT_SUCCESS;
}
