#pragma once

class ParserState;

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
bool GenerateFilterFunctions(Context* ctx, ParserState* state, StreamGraph::Node* root_node);
}
