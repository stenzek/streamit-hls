#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common/types.h"

class ParserState;

namespace llvm
{
class Constant;
class Type;
}

namespace AST
{
class FilterDeclaration;
class ParameterDeclaration;
}

namespace Frontend
{
class WrappedLLVMContext;
}

namespace StreamGraph
{
class BuilderState;
class FilterPermutation;
class Node;
class Filter;
class Pipeline;
class SplitJoin;
class Split;
class Join;
using NodeList = std::vector<Node*>;
using StringList = std::vector<std::string>;

class StreamGraph
{
public:
  using FilterInstanceList = std::vector<Filter*>;
  using FilterPermutationList = std::vector<FilterPermutation*>;

  StreamGraph(Node* root, const FilterPermutationList& filter_permutation_list, Node* program_input_node,
              Node* program_output_node);
  ~StreamGraph();

  Node* GetRootNode() const { return m_root_node; }
  std::string Dump();

  // Get a list of all filter instances in the graph
  FilterInstanceList GetFilterInstanceList() const;

  // Get a list of all unique filter (parameter permutations) in the graph
  const FilterPermutationList& GetFilterPermutationList() const { return m_filter_permutations; }

  // Input/output types of the whole program
  Node* GetProgramInputNode() const { return m_program_input_node; }
  Node* GetProgramOutputNode() const { return m_program_output_node; }
  bool HasProgramInputNode() const { return (m_program_input_node != nullptr); }
  bool HasProgramOutputNode() const { return (m_program_output_node != nullptr); }
  llvm::Type* GetProgramInputType() const;
  llvm::Type* GetProgramOutputType() const;
  u32 GetProgramInputWidth() const;
  u32 GetProgramOutputWidth() const;

  // Finds the predecessor, or node which outputs to the specified node.
  Node* GetSinglePredecessor(Node* node) const;
  NodeList GetPredecessors(Node* node) const;

  // Widens channels where possible.
  void WidenChannels();

private:
  void WidenInput();
  void WidenOutput();

  Node* m_root_node;
  FilterPermutationList m_filter_permutations;
  Node* m_program_input_node;
  Node* m_program_output_node;
};

std::unique_ptr<StreamGraph> BuildStreamGraph(Frontend::WrappedLLVMContext* context, ParserState* parser);

class Visitor
{
public:
  virtual bool Visit(Filter* node) { return true; }
  virtual bool Visit(Pipeline* node) { return true; }
  virtual bool Visit(SplitJoin* node) { return true; }
  virtual bool Visit(Split* node) { return true; }
  virtual bool Visit(Join* node) { return true; }
};

class FilterParameters
{
public:
  struct Parameter
  {
    const AST::ParameterDeclaration* decl;
    size_t data_offset;
    size_t data_length;
    llvm::Constant* value;
  };

  FilterParameters() = default;
  ~FilterParameters() = default;

  void AddParameter(const AST::ParameterDeclaration* decl, const void* data, size_t data_len, llvm::Constant* value);

  std::vector<Parameter>::const_iterator begin() const { return m_params.begin(); }
  std::vector<Parameter>::const_iterator end() const { return m_params.end(); }
  const Parameter& GetParameter(size_t i) { return m_params.at(i); }

  bool operator==(const FilterParameters& rhs) const;
  bool operator!=(const FilterParameters& rhs) const;

private:
  std::vector<unsigned char> m_data;
  std::vector<Parameter> m_params;
};

class FilterPermutation
{
public:
  FilterPermutation(const std::string& name, const AST::FilterDeclaration* filter_decl,
                    const FilterParameters& filter_params, llvm::Type* input_type, llvm::Type* output_type,
                    int peek_rate, int pop_rate, int push_rate, u32 input_channel_width, u32 output_channel_width);
  ~FilterPermutation() = default;

  const std::string& GetName() const { return m_name; }
  const AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }
  const FilterParameters& GetFilterParameters() const { return m_filter_params; }
  llvm::Type* GetInputType() const { return m_input_type; }
  llvm::Type* GetOutputType() const { return m_output_type; }
  int GetPeekRate() const { return m_peek_rate; }
  int GetPopRate() const { return m_pop_rate; }
  int GetPushRate() const { return m_push_rate; }
  u32 GetInputChannelWidth() const { return m_input_channel_width; }
  u32 GetOutputChannelWidth() const { return m_output_channel_width; }

  bool IsBuiltin() const;
  bool IsCombinational() const { return m_combinational; }
  void SetCombinational() { m_combinational = true; }

private:
  std::string m_name;
  const AST::FilterDeclaration* m_filter_decl;
  FilterParameters m_filter_params;
  llvm::Type* m_input_type;
  llvm::Type* m_output_type;
  int m_peek_rate;
  int m_pop_rate;
  int m_push_rate;
  u32 m_input_channel_width;
  u32 m_output_channel_width;
  bool m_combinational = false;
};

class Node
{
  friend StreamGraph;

public:
  Node(const std::string& name, llvm::Type* input_type, llvm::Type* output_type);
  virtual ~Node() = default;

  const std::string& GetName() const { return m_name; }
  llvm::Type* GetInputType() const { return m_input_type; }
  llvm::Type* GetOutputType() const { return m_output_type; }
  u32 GetPeekRate() const { return m_peek_rate; }
  u32 GetPopRate() const { return m_pop_rate; }
  u32 GetPushRate() const { return m_push_rate; }
  u32 GetNetPeek() const { return m_peek_rate * m_multiplicity; }
  u32 GetNetPop() const { return m_pop_rate * m_multiplicity; }
  u32 GetNetPush() const { return m_push_rate * m_multiplicity; }
  u32 GetMultiplicity() const { return m_multiplicity; }

  virtual bool Accept(Visitor* visitor) = 0;
  virtual bool AddChild(BuilderState* state, Node* child) = 0;
  virtual bool Validate(BuilderState* state) = 0;

  // Channel creation - we call this method on the source node
  virtual bool ConnectTo(BuilderState* state, Node* dst) = 0;

  // Gets the first filter/node in the pipeline/splitjoin that should be connected to
  virtual Node* GetInputNode() = 0;
  virtual std::string GetInputChannelName() = 0;

  // Schedule for steady state
  virtual void SteadySchedule() = 0;
  virtual void AddMultiplicity(u32 count) = 0;

  // Channel widening
  virtual void SetInputChannelWidth(u32 width) = 0;
  virtual void WidenChannels() = 0;

protected:
  std::string m_name;
  llvm::Type* m_input_type;
  llvm::Type* m_output_type;
  u32 m_peek_rate = 0;
  u32 m_pop_rate = 0;
  u32 m_push_rate = 0;
  u32 m_multiplicity = 1;
};

class Filter : public Node
{
  friend StreamGraph;

public:
  Filter(const std::string& instance_name, const FilterPermutation* filter);
  ~Filter() = default;

  const FilterPermutation* GetFilterPermutation() const { return m_filter_permutation; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }
  Node* GetOutputConnection() const { return m_output_connection; }
  const std::string& GetOutputChannelName() const { return m_output_channel_name; }
  u32 GetInputChannelWidth() const { return m_input_channel_width; }
  u32 GetOutputChannelWidth() const { return m_output_channel_width; }

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* child) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

  void SetInputChannelWidth(u32 width) override;
  void WidenChannels() override;

protected:
  const FilterPermutation* m_filter_permutation;
  Node* m_output_connection = nullptr;
  std::string m_output_channel_name;
  u32 m_input_channel_width;
  u32 m_output_channel_width;
};

class Pipeline : public Node
{
public:
  Pipeline(const std::string& name);
  ~Pipeline() = default;

  const NodeList& GetChildren() const { return m_children; }

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

  void SetInputChannelWidth(u32 width) override;
  void WidenChannels() override;

protected:
  NodeList m_children;
};

class SplitJoin : public Node
{
public:
  SplitJoin(const std::string& name);
  ~SplitJoin() = default;

  const NodeList& GetChildren() const { return m_children; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }
  Node* GetOutputConnection() const { return m_output_connection; }
  Split* GetSplitNode() const { return m_split_node; }
  Join* GetJoinNode() const { return m_join_node; }

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

  void SetInputChannelWidth(u32 width) override;
  void WidenChannels() override;

protected:
  NodeList m_children;
  Node* m_output_connection = nullptr;
  Split* m_split_node = nullptr;
  Join* m_join_node = nullptr;
};

class Split : public Node
{
public:
  friend SplitJoin;
  enum class Mode
  {
    Duplicate,
    Roundrobin
  };

  Split(const std::string& name, Mode mode, const std::vector<int>& distribution);
  ~Split() = default;

  const NodeList& GetOutputs() const { return m_outputs; }
  const StringList& GetOutputChannelNames() const { return m_output_channel_names; }
  const u32 GetNumOutputChannels() const { return u32(m_outputs.size()); }
  const Mode GetMode() const { return m_mode; }
  const std::vector<int>& GetDistribution() const { return m_distribution; }
  std::vector<int>& GetDistribution() { return m_distribution; }
  u32 GetInputChannelWidth() const { return m_input_channel_width; }
  void SetDataType(llvm::Type* type);
  u32 GetDistributionSum() const;

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

  void SetInputChannelWidth(u32 width) override;
  void WidenChannels() override;

private:
  NodeList m_outputs;
  StringList m_output_channel_names;
  Mode m_mode;
  std::vector<int> m_distribution;
  u32 m_input_channel_width = 1;
};

class Join : public Node
{
public:
  friend SplitJoin;
  Join(const std::string& name, const std::vector<int>& distribution);
  ~Join() = default;

  Node* GetOutputConnection() const { return m_output_connection; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }
  const std::string& GetOutputChannelName() const { return m_output_channel_name; }
  const std::vector<int>& GetDistribution() const { return m_distribution; }
  std::vector<int>& GetDistribution() { return m_distribution; }
  u32 GetOutputChannelWidth() const { return m_output_channel_width; }

  u32 GetIncomingStreams() const { return m_incoming_streams; }
  void AddIncomingStream();
  void SetDataType(llvm::Type* type);
  u32 GetDistributionSum() const;

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

  void SetInputChannelWidth(u32 width) override;
  void WidenChannels() override;

private:
  Node* m_output_connection = nullptr;
  std::string m_output_channel_name;
  u32 m_incoming_streams = 0;
  std::vector<int> m_distribution;
  u32 m_output_channel_width = 1;
};
}
