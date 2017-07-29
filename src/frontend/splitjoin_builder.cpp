#include "frontend/splitjoin_builder.h"
#include <cassert>
#include "common/string_helpers.h"
#include "frontend/context.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "parser/type.h"

namespace Frontend
{

SplitJoinBuilder::SplitJoinBuilder(Context* context, llvm::Module* mod, const std::string& instance_name)
  : m_context(context), m_module(mod), m_instance_name(instance_name)
{
}

SplitJoinBuilder::~SplitJoinBuilder()
{
}

bool SplitJoinBuilder::GenerateSplit(StreamGraph::Split* split, int mode)
{
  return (GenerateSplitGlobals(split, mode) && GenerateSplitPushFunction(split, mode) &&
          GenerateSplitGetSizeFunction(split, mode) && GenerateSplitGetSpaceFunction(split, mode));
}

bool SplitJoinBuilder::GenerateJoin(StreamGraph::Join* join)
{
  return (GenerateJoinGlobals(join) && GenerateJoinPushFunction(join) && GenerateJoinGetSizeFunction(join) &&
          GenerateJoinGetSpaceFunction(join));
}

bool SplitJoinBuilder::GenerateSplitGlobals(StreamGraph::Split* split, int mode)
{
  // last if roundrobin
  return true;
}

bool SplitJoinBuilder::GenerateSplitPushFunction(StreamGraph::Split* split, int mode)
{
  return true;
}

bool SplitJoinBuilder::GenerateSplitGetSizeFunction(StreamGraph::Split* split, int mode)
{
  return true;
}

bool SplitJoinBuilder::GenerateSplitGetSpaceFunction(StreamGraph::Split* split, int mode)
{
  return true;
}

bool SplitJoinBuilder::GenerateJoinGlobals(StreamGraph::Join* join)
{
  return true;
}

bool SplitJoinBuilder::GenerateJoinPushFunction(StreamGraph::Join* join)
{
  return true;
}

bool SplitJoinBuilder::GenerateJoinGetSizeFunction(StreamGraph::Join* join)
{
  return true;
}

bool SplitJoinBuilder::GenerateJoinGetSpaceFunction(StreamGraph::Join* join)
{
  return true;
}

} // namespace Frontend
