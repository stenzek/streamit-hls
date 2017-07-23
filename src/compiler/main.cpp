#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include "frontend/frontend.h"
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
#include "spdlog/spdlog.h"

auto frontend_log = spdlog::stdout_color_mt("frontend");
auto parser_log = spdlog::stdout_color_mt("parser");
auto main_log = spdlog::stdout_color_mt("main");

int main(int argc, char* argv[])
{
  const char* filename = "stdin";
  std::FILE* fp = stdin;

  frontend_log->set_level(spdlog::level::debug);
  parser_log->set_level(spdlog::level::debug);
  main_log->set_level(spdlog::level::debug);

  if (argc > 1)
  {
    filename = argv[1];
    fp = fopen(filename, "r");
    if (!fp)
    {
      main_log->error("Failed to open file {}", filename);
      return EXIT_FAILURE;
    }
  }

  ParserState state;
  if (!state.ParseFile(filename, fp))
  {
    main_log->error("Parse failed. Exiting.");
    return EXIT_FAILURE;
  }

  state.DumpAST();

  Frontend::Context* ctx = Frontend::CreateContext();
  if (!Frontend::GenerateStreamGraph(ctx, &state))
  {
    main_log->error("Generating stream graph failed. Exiting.");
    Frontend::DestroyContext(ctx);
    return EXIT_FAILURE;
  }

  if (!Frontend::GenerateFilterFunctions(ctx, &state))
  {
    main_log->error("Generating code failed. Exiting.");
    Frontend::DestroyContext(ctx);
    return EXIT_FAILURE;
  }

  Frontend::DestroyContext(ctx);
  return EXIT_SUCCESS;
}
