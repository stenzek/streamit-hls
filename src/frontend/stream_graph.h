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
  const NodeList& GetInputs() const { return m_inputs; }
  const NodeList& GetOutputs() const { return m_outputs; }

  void AddInput(Node* input) { m_inputs.push_back(input); }
  void AddOutput(Node* output) { m_outputs.push_back(output); }

  virtual bool Accept(Visitor* visitor) = 0;
  virtual bool AddChild(BuilderState* state, Node* child) = 0;
  virtual bool ConnectTo(BuilderState* state, Node* dst) = 0;
  virtual bool ConnectFrom(BuilderState* state, Node* src) = 0;
  virtual bool Validate(BuilderState* state) = 0;

  virtual Node* GetChildInputNode() = 0;
  virtual Node* GetChildOutputNode() = 0;

protected:
  std::string m_name;
  const Type* m_input_type;
  const Type* m_output_type;
  NodeList m_inputs;
  NodeList m_outputs;
};

class Filter : public Node
{
public:
  Filter(AST::FilterDeclaration* decl, const std::string& name, const Type* input_type, const Type* output_type);
  ~Filter() = default;

  AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }

  bool Accept(Visitor* visitor) override;
  bool ConnectTo(BuilderState* state, Node* dst) override;
  bool ConnectFrom(BuilderState* state, Node* src) override;
  bool AddChild(BuilderState* state, Node* child) override;
  bool Validate(BuilderState* state) override;

  Node* GetChildInputNode() override;
  Node* GetChildOutputNode() override;

protected:
  AST::FilterDeclaration* m_filter_decl;
};

class Pipeline : public Node
{
public:
  Pipeline(const std::string& name);
  ~Pipeline() = default;

  const NodeList& GetChildren() const { return m_children; }

  bool Accept(Visitor* visitor) override;
  bool ConnectTo(BuilderState* state, Node* dst) override;
  bool ConnectFrom(BuilderState* state, Node* src) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  Node* GetChildInputNode() override;
  Node* GetChildOutputNode() override;

protected:
  NodeList m_children;
};

class SplitJoin : public Node
{
public:
  SplitJoin(const std::string& name);
  ~SplitJoin() = default;

  const NodeList& GetChildren() const { return m_children; }
  Split* GetSplitNode() const { return m_split_node; }
  Join* GetJoinNode() const { return m_join_node; }

  bool Accept(Visitor* visitor) override;
  bool ConnectTo(BuilderState* state, Node* dst) override;
  bool ConnectFrom(BuilderState* state, Node* src) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  Node* GetChildInputNode() override;
  Node* GetChildOutputNode() override;

protected:
  NodeList m_children;
  Split* m_split_node = nullptr;
  Join* m_join_node = nullptr;
};

class Split : public Node
{
public:
  // TODO: Mode
  Split(const std::string& name);
  ~Split() = default;

  const NodeList& GetChildren() const { return m_children; }

  bool Accept(Visitor* visitor) override;
  bool ConnectTo(BuilderState* state, Node* dst) override;
  bool ConnectFrom(BuilderState* state, Node* src) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  Node* GetChildInputNode() override;
  Node* GetChildOutputNode() override;

private:
  NodeList m_children;
};

class Join : public Node
{
public:
  Join(const std::string& name);
  ~Join() = default;

  bool Accept(Visitor* visitor) override;
  bool ConnectTo(BuilderState* state, Node* dst) override;
  bool ConnectFrom(BuilderState* state, Node* src) override;
  bool AddChild(BuilderState* state, Node* node) override;
  bool Validate(BuilderState* state) override;

  Node* GetChildInputNode() override;
  Node* GetChildOutputNode() override;
};
}
