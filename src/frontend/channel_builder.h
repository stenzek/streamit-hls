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
class Filter;
class Split;
class Join;
}

namespace Frontend
{
class Context;

class ChannelBuilder
{
public:
  ChannelBuilder(Context* context, llvm::Module* mod, const std::string& instance_name);
  ~ChannelBuilder();

  Context* GetContext() const { return m_context; }

  // TODO: Enum for mode, 0=roundrobin, 1=duplicate
  bool GenerateCode(StreamGraph::Filter* filter);
  bool GenerateCode(StreamGraph::Split* split, int mode);
  bool GenerateCode(StreamGraph::Join* join);

private:
  bool GenerateFilterGlobals(StreamGraph::Filter* filter);
  bool GenerateFilterPeekFunction(StreamGraph::Filter* filter);
  bool GenerateFilterPopFunction(StreamGraph::Filter* filter);
  bool GenerateFilterPushFunction(StreamGraph::Filter* filter);

  bool GenerateSplitGlobals(StreamGraph::Split* split, int mode);
  bool GenerateSplitPushFunction(StreamGraph::Split* split, int mode);

  bool GenerateJoinGlobals(StreamGraph::Join* join);
  bool GenerateJoinSyncFunction(StreamGraph::Join* join);
  bool GenerateJoinPushFunction(StreamGraph::Join* join);

  Context* m_context;
  llvm::Module* m_module;
  std::string m_instance_name;

  llvm::Type* m_input_buffer_type = nullptr;
  llvm::GlobalVariable* m_input_buffer_var = nullptr;
  llvm::GlobalVariable* m_last_index_var = nullptr;
};

} // namespace Frontend