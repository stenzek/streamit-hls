#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
#include "frontend/context.h"
#include "frontend/filter_builder.h"
#include "frontend/stream_graph_builder.h"
#include "spdlog/spdlog.h"

extern bool temp_codegenerator_run(ParserState*);

int main(int argc, char* argv[])
{
  const char* filename = "stdin";
  std::FILE* fp = stdin;

  auto frontend_log = spdlog::stdout_color_mt("frontend");
  auto parser_log = spdlog::stdout_color_mt("parser");
  auto log = spdlog::stdout_color_mt("main");
  frontend_log->set_level(spdlog::level::debug);
  parser_log->set_level(spdlog::level::debug);
  log->set_level(spdlog::level::debug);

  if (argc > 1)
  {
    filename = argv[1];
    fp = fopen(filename, "r");
    if (!fp)
    {
      log->error("Failed to open file {}", filename);
      return EXIT_FAILURE;
    }
  }

  ParserState state;
  if (!state.ParseFile(filename, fp))
  {
    log->error("Parse failed. Exiting.");
    return EXIT_FAILURE;
  }

  state.DumpAST();

  if (!temp_codegenerator_run(&state))
  {
    log->error("Generating code failed. Exiting.");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
