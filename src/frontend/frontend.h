#pragma once

class ParserState;

namespace llvm
{
class Module;
}
namespace Frontend
{
class Context;
}
namespace StreamGraph
{
class Node;
}

namespace Frontend
{
Context* CreateContext();
void DestroyContext(Context* ctx);

StreamGraph::Node* GenerateStreamGraph(Context* ctx, ParserState* state);
bool GenerateCode(Context* ctx, ParserState* state, StreamGraph::Node* root_node);

bool GenerateFilterFunctions(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* root_node);
bool GeneratePrimePumpFunction(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* root_node);
bool GenerateSteadyStateFunction(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* root_node);
bool GenerateMainFunction(Context* ctx, llvm::Module* mod, ParserState* state, StreamGraph::Node* node);
bool ExecuteMainFunction(Context* ctx, llvm::Module* mod);
}
