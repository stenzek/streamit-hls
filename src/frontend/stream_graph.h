#pragma once
#include <string>
#include <vector>

class Type;

namespace AST
{
class FilterDeclaration;
}

namespace StreamGraph
{

class Node;
class Filter;
class Pipeline;
class SplitJoin;
class Split;
class Join;
using NodeList = std::vector<Node*>;
std::string DumpStreamGraph(Node* root);

class BuilderState;

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

  virtual bool Accept(Visitor* visitor) = 0;
  virtual bool AddChild(BuilderState* state, Node* child) = 0;
  virtual bool Validate(BuilderState* state) = 0;

  // Channel creation - we call this method on the source node
  virtual bool ConnectTo(BuilderState* state, Node* dst) = 0;

  // Gets the first filter/node in the pipeline/splitjoin that should be connected to
  virtual Node* GetInputNode() = 0;

protected:
  std::string m_name;
  const Type* m_input_type;
  const Type* m_output_type;
};

class Filter : public Node
{
public:
  Filter(AST::FilterDeclaration* decl, const std::string& name, const Type* input_type, const Type* output_type);
  ~Filter() = default;

  AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }
  Node* GetOutputConnection() const { return m_output_connection; }

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* child) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;

protected:
  AST::FilterDeclaration* m_filter_decl;
  Node* m_output_connection = nullptr;
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

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;

private:
  NodeList m_outputs;
};

class Join : public Node
{
public:
  Join(const std::string& name);
  ~Join() = default;

  Node* GetOutputConnection() const { return m_output_connection; }
  bool HasOutputConnection() const { return (m_output_connection != nullptr); }

  bool Accept(Visitor* visitor) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  bool ConnectTo(BuilderState* state, Node* dst) override;
  Node* GetInputNode() override;

private:
  Node* m_output_connection = nullptr;
};
}