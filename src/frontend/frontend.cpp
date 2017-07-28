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

StreamGraph::Node* GenerateStreamGraph(Frontend::Context* ctx, ParserState* state)
{
  Log::Info("frontend", "Generating stream graph...");

  StreamGraph::Builder builder(ctx, state);
  if (!builder.GenerateGraph())
    return nullptr;

  std::string graph = StreamGraph::DumpStreamGraph(builder.GetStartNode());
  std::cout << graph << std::endl;
  return builder.GetStartNode();
}

class FilterGeneratorVisitor : public StreamGraph::Visitor
{
public:
  FilterGeneratorVisitor(Context* context, llvm::Module* module) : m_context(context), m_module(module) {}

  virtual bool Visit(StreamGraph::Filter* node) override;
  virtual bool Visit(StreamGraph::Pipeline* node) override;
  virtual bool Visit(StreamGraph::SplitJoin* node) override;
  virtual bool Visit(StreamGraph::Split* node) override;
  virtual bool Visit(StreamGraph::Join* node) override;

private:
  Context* m_context;
  llvm::Module* m_module;
};

bool FilterGeneratorVisitor::Visit(StreamGraph::Filter* node)
{
  m_context->LogInfo("Generating filter function set %s for %s", node->GetName().c_str(),
                     node->GetFilterDeclaration()->GetName().c_str());

  // Generate function for filter node
  FilterBuilder fb(m_context, m_module, node->GetFilterDeclaration(), node->GetName(),
                   node->HasOutputConnection() ? node->GetOutputConnection()->GetName() : "");
  if (!fb.GenerateCode())
    return false;

  // Generate fifo queue for the input side of this filter
  return true;
}

bool FilterGeneratorVisitor::Visit(StreamGraph::Pipeline* node)
{
  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

bool FilterGeneratorVisitor::Visit(StreamGraph::SplitJoin* node)
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

bool FilterGeneratorVisitor::Visit(StreamGraph::Split* node)
{
  return true;
}

bool FilterGeneratorVisitor::Visit(StreamGraph::Join* node)
{
  return true;
}

bool GenerateFilterFunctions(Frontend::Context* ctx, ParserState* state, StreamGraph::Node* root_node)
{
  Log::Info("frontend", "Generating filter functions...");

  std::unique_ptr<llvm::Module> mod = ctx->CreateModule("filters");

  FilterGeneratorVisitor fgv(ctx, mod.get());
  bool result = root_node->Accept(&fgv);

  ctx->DumpModule(mod.get());
  if (!ctx->VerifyModule(mod.get()))
    Log::Error("frontend", "Filter module verification failed");

  return result;
}
}