#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common/types.h"

class Type;
class ParserState;
class WrappedLLVMContext;

namespace AST
{
class FilterDeclaration;
}

namespace StreamGraph
{
class BuilderState;
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
  StreamGraph(Node* root);
  ~StreamGraph();

  Node* GetRootNode() const { return m_root_node; }
  std::string Dump();

  // Get a list of all filter instances in the graph
  using FilterInstanceList = std::vector<Filter*>;
  FilterInstanceList GetFilterInstanceList() const;

  // Get a list of all unique filter (parameter permutations) in the graph
  using FilterPermutationList = std::vector<std::pair<AST::FilterDeclaration*, std::string>>;
  FilterPermutationList GetFilterPermutationList() const;

private:
  Node* m_root_node;
};

std::unique_ptr<StreamGraph> BuildStreamGraph(WrappedLLVMContext* context, ParserState* parser);

class Visitor
{
public:
  virtual bool Visit(Filter* node) { return true; }
  virtual bool Visit(Pipeline* node) { return true; }
  virtual bool Visit(SplitJoin* node) { return true; }
  virtual bool Visit(Split* node) { return true; }
  virtual bool Visit(Join* node) { return true; }
};

class Node
{
public:
  Node(const std::string& name, const Type* input_type, const Type* output_type);
  virtual ~Node() = default;

  const std::string& GetName() const { return m_name; }
  const Type* GetInputType() const { return m_input_type; }
  const Type* GetOutputType() const { return m_output_type; }
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

protected:
  std::string m_name;
  const Type* m_input_type;
  const Type* m_output_type;
  u32 m_peek_rate = 0;
  u32 m_pop_rate = 0;
  u32 m_push_rate = 0;
  u32 m_multiplicity = 1;
};

class Filter : public Node
{
public:
  Filter(AST::FilterDeclaration* decl, const std::string& name);
  ~Filter() = default;

  AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }
  Node* GetOutputConnection() const { return m_output_connection; }
  const std::string& GetOutputChannelName() const { return m_output_channel_name; }

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* child) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

protected:
  AST::FilterDeclaration* m_filter_decl;
  Node* m_output_connection = nullptr;
  std::string m_output_channel_name;
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

protected:
  NodeList m_children;
  Node* m_output_connection = nullptr;
  Split* m_split_node = nullptr;
  Join* m_join_node = nullptr;
};

class Split : public Node
{
public:
  // TODO: Mode
  Split(const std::string& name);
  ~Split() = default;

  const NodeList& GetOutputs() const { return m_outputs; }
  const StringList& GetOutputChannelNames() const { return m_output_channel_names; }
  void SetDataType(const Type* type);

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

private:
  NodeList m_outputs;
  StringList m_output_channel_names;
};

class Join : public Node
{
public:
  Join(const std::string& name);
  ~Join() = default;

  Node* GetOutputConnection() const { return m_output_connection; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }
  const std::string& GetOutputChannelName() const { return m_output_channel_name; }

  u32 GetIncomingStreams() const { return m_incoming_streams; }
  void AddIncomingStream();
  void SetDataType(const Type* type);

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;
  std::string GetInputChannelName() override;

  void SteadySchedule() override;
  void AddMultiplicity(u32 count) override;

private:
  Node* m_output_connection = nullptr;
  std::string m_output_channel_name;
  u32 m_incoming_streams = 0;
};
}
