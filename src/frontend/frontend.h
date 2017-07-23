#pragma once

class ParserState;

namespace Frontend
{
class Context;

Context* CreateContext();
void DestroyContext(Context* ctx);

bool GenerateStreamGraph(Context* ctx, ParserState* state);
bool GenerateFilterFunctions(Context* ctx, ParserState* state);
}
