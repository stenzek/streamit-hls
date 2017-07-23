#include <iostream>
#include "common/log.h"
#include "frontend/context.h"
#include "frontend/filter_builder.h"
#include "frontend/stream_graph.h"
#include "frontend/stream_graph_builder.h"
#include "llvm/IR/Module.h"
#include "parser/parser_state.h"

namespace Frontend
{

Context* CreateContext()
{
  return new Context();
}

void DestroyContext(Context* ctx)
{
  delete ctx;
}

bool GenerateStreamGraph(Frontend::Context* ctx, ParserState* state)
{
  Log::Info("frontend", "Generating stream graph...");

  StreamGraph::Builder builder(ctx, state);
  if (!builder.GenerateGraph())
    return false;

  std::string graph = StreamGraph::DumpStreamGraph(builder.GetStartNode());
  std::cout << graph << std::endl;
  return true;
}

bool GenerateFilterFunctions(Frontend::Context* ctx, ParserState* state)
{
  Log::Info("frontend", "Generating filter functions...");

  std::unique_ptr<llvm::Module> mod = ctx->CreateModule("filters");

  bool result = true;
  for (AST::FilterDeclaration* filter_decl : state->GetFilterList())
  {
    Frontend::FilterBuilder fb(ctx, mod.get(), filter_decl);
    result &= fb.GenerateCode();
  }

  ctx->DumpModule(mod.get());
  if (!ctx->VerifyModule(mod.get()))
    Log::Error("frontend", "Filter module verification failed");

  return result;
}
}