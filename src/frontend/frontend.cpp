#include "frontend/frontend.h"
#include <iostream>
#include "common/log.h"
#include "frontend/channel_builder.h"
#include "frontend/context.h"
#include "frontend/filter_builder.h"
#include "frontend/main_loop_builder.h"
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

StreamGraph::Node* GenerateStreamGraph(Frontend::Context* ctx, ParserState* state)
{
  Log::Info("frontend", "Generating stream graph...");

  StreamGraph::Builder builder(ctx, state);
  if (!builder.GenerateGraph())
    return nullptr;

  StreamGraph::Node* root_node = builder.GetStartNode();
  root_node->SteadySchedule();

  std::string graph = StreamGraph::DumpStreamGraph(root_node);
  std::cout << graph << std::endl;
  return builder.GetStartNode();
}

bool GenerateCode(Context* ctx, ParserState* state, StreamGraph::Node* root_node)
{
  llvm::Module* mod = ctx->CreateModule("filters");
  if (!GenerateFilterFunctions(ctx, mod, state, root_node) || !GeneratePrimePumpFunction(ctx, mod, state, root_node) ||
      !GenerateSteadyStateFunction(ctx, mod, state, root_node) || !GenerateMainFunction(ctx, mod, state, root_node))
  {
    return false;
  }

  ctx->DumpModule(mod);
  if (!ctx->VerifyModule(mod))
    Log::Error("frontend", "Module verification failed.");

  ctx->DestroyModule(mod);
}

class CodeGeneratorVisitor : public StreamGraph::Visitor
{
public:
  CodeGeneratorVisitor(Context* context, llvm::Module* module) : m_context(context), m_module(module) {}

  virtual bool Visit(StreamGraph::Filter* node) override;
  virtual bool Visit(StreamGraph::Pipeline* node) override;
  virtual bool Visit(StreamGraph::SplitJoin* node) override;
  virtual bool Visit(StreamGraph::Split* node) override;
  virtual bool Visit(StreamGraph::Join* node) override;

private:
  Context* m_context;
  llvm::Module* m_module;
};

bool CodeGeneratorVisitor::Visit(StreamGraph::Filter* node)
{
  m_context->LogInfo("Generating filter function set %s for %s", node->GetName().c_str(),
                     node->GetFilterDeclaration()->GetName().c_str());

  // Generate function for filter node
  FilterBuilder fb(m_context, m_module, node->GetFilterDeclaration(), node->GetName(), node->GetOutputChannelName());
  if (!fb.GenerateCode())
    return false;

  // Generate fifo queue for the input side of this filter
  ChannelBuilder cb(m_context, m_module, node->GetName());
  if (!cb.GenerateCode(node))
    return false;

  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Pipeline* node)
{
  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::SplitJoin* node)
{
  if (!node->GetSplitNode()->Accept(this))
    return false;

  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  if (!node->GetJoinNode()->Accept(this))
    return false;

  return true;
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Split* node)
{
  ChannelBuilder cb(m_context, m_module, node->GetName());
  return cb.GenerateCode(node, 1);
}

bool CodeGeneratorVisitor::Visit(StreamGraph::Join* node)
{
  ChannelBuilder cb(m_context, m_module, node->GetName());
  return cb.GenerateCode(node);
}

bool GenerateFilterFunctions(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* root_node)
{
  Log::Info("frontend", "Generating filter functions...");

  CodeGeneratorVisitor fgv(ctx, mod);
  return root_node->Accept(&fgv);
}

bool GeneratePrimePumpFunction(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* root_node)
{
  Log::Info("frontend", "Generating prime pump function...");

  MainLoopBuilder builder(ctx, mod, "mod");
  return builder.GeneratePrimePumpFunction(root_node);
}

bool GenerateSteadyStateFunction(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* root_node)
{
  Log::Info("frontend", "Generating steady state function...");
  return true;
}

bool GenerateMainFunction(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* node)
{
  Log::Info("frontend", "Generating main function...");
  return true;
}
}