#pragma once
#include <unordered_map>

namespace llvm
{
class Constant;
class Function;
class GlobalVariable;
class Module;
class Type;
}

namespace StreamGraph
{
class Split;
class Join;
}

namespace Frontend
{
class Context;

class SplitJoinBuilder
{
public:
  SplitJoinBuilder(Context* context, llvm::Module* mod, const std::string& instance_name);
  ~SplitJoinBuilder();

  Context* GetContext() const { return m_context; }

  // TODO: Enum for mode, 0=roundrobin, 1=duplicate
  bool GenerateSplit(StreamGraph::Split* split, int mode);
  bool GenerateJoin(StreamGraph::Join* join);

private:
  bool GenerateSplitGlobals(StreamGraph::Split* split, int mode);
  bool GenerateSplitPushFunction(StreamGraph::Split* split, int mode);
  bool GenerateSplitGetSizeFunction(StreamGraph::Split* split, int mode);
  bool GenerateSplitGetSpaceFunction(StreamGraph::Split* split, int mode);
  bool GenerateJoinGlobals(StreamGraph::Join* join);
  bool GenerateJoinPushFunction(StreamGraph::Join* join);
  bool GenerateJoinGetSizeFunction(StreamGraph::Join* join);
  bool GenerateJoinGetSpaceFunction(StreamGraph::Join* join);

  Context* m_context;
  llvm::Module* m_module;
  std::string m_instance_name;
  std::string m_output_instance_name;

  llvm::GlobalVariable* m_split_last_index_var = nullptr;
  llvm::GlobalVariable* m_join_last_index_var = nullptr;
};

} // namespace Frontend