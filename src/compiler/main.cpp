#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include "frontend/context.h"
#include "frontend/filter_builder.h"
#include "frontend/stream_graph_builder.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
#include "spdlog/spdlog.h"

auto frontend_log = spdlog::stdout_color_mt("frontend");
auto parser_log = spdlog::stdout_color_mt("parser");
auto main_log = spdlog::stdout_color_mt("main");

static bool GenerateStreamGraph(Frontend::Context* ctx, ParserState* state)
{
  main_log->info("Generating stream graph...");

  Frontend::StreamGraphBuilder builder(ctx, state);
  if (!builder.GenerateGraph())
    return false;

  return true;
}

static bool GenerateFilterFunctions(Frontend::Context* ctx, ParserState* state)
{
  main_log->info("Generating filter functions...");

  std::unique_ptr<llvm::Module> mod = ctx->CreateModule("filters");

  bool result = true;
  for (AST::FilterDeclaration* filter_decl : state->GetFilterList())
  {
    Frontend::FilterBuilder fb(ctx, mod.get(), filter_decl);
    result &= fb.GenerateCode();
  }

  ctx->DumpModule(mod.get());
  if (!ctx->VerifyModule(mod.get()))
    main_log->error("Filter module verification failed");

  return result;
}

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

  std::unique_ptr<Frontend::Context> ctx = std::make_unique<Frontend::Context>();
  if (!GenerateStreamGraph(ctx.get(), &state))
  {
    main_log->error("Generating stream graph failed. Exiting.");
    return EXIT_FAILURE;
  }

  if (!GenerateFilterFunctions(ctx.get(), &state))
  {
    main_log->error("Generating code failed. Exiting.");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
