#include "frontend/stream_graph.h"
#include <cassert>
#include "frontend/stream_graph_builder.h"

namespace StreamGraph
{
static bool IsStream(Node* node)
{
  return (dynamic_cast<Filter*>(node) != nullptr || dynamic_cast<Pipeline*>(node) != nullptr ||
          dynamic_cast<SplitJoin*>(node) != nullptr);
}

Node::Node(const std::string& name, const Type* input_type, const Type* output_type)
  : m_name(name), m_input_type(input_type), m_output_type(output_type)
{
}

Filter::Filter(AST::FilterDeclaration* decl, const std::string& name, const Type* input_type, const Type* output_type)
  : Node(name, input_type, output_type), m_filter_decl(decl)
{
}

bool Filter::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool Filter::AddChild(BuilderState* state, Node* child)
{
  assert(0 && "AddChild() called with filter on stack.");
  return true;
}

bool Filter::ConnectFrom(BuilderState* state, Node* src)
{
  AddInput(src);
  return true;
}

bool Filter::ConnectTo(BuilderState* state, Node* dst)
{
  AddOutput(dst);
  return true;
}

bool Filter::Validate(BuilderState* state)
{
  return true;
}

Node* Filter::GetChildInputNode()
{
  return this;
}

Node* Filter::GetChildOutputNode()
{
  return this;
}

Pipeline::Pipeline(const std::string& name) : Node(name, nullptr, nullptr)
{
}

bool Pipeline::AddChild(BuilderState* state, Node* node)
{
  if (!IsStream(node))
  {
    state->Error("Adding non-stream to pipeline");
    return false;
  }

  if (!m_children.empty())
  {
    Node* last = m_children.back();
    if (!last->GetChildOutputNode()->ConnectTo(state, node->GetChildInputNode()) ||
        !node->GetChildInputNode()->ConnectFrom(state, last->GetChildOutputNode()))
      return false;
  }

  m_children.push_back(node);
  return true;
}

bool Pipeline::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool Pipeline::ConnectTo(BuilderState* state, Node* dst)
{
  if (m_children.empty())
  {
    state->Error("Connecting empty pipeline to sink.");
    return false;
  }

  Node* back = m_children.back();
  if (!back->ConnectTo(state, dst->GetChildInputNode()))
    return false;

  return true;
}

bool Pipeline::ConnectFrom(BuilderState* state, Node* src)
{
  if (m_children.empty())
  {
    state->Error("Connecting sink to empty pipeline");
    return false;
  }

  Node* front = m_children.front();
  if (!front->ConnectFrom(state, src->GetChildOutputNode()))
    return false;

  return true;
}

bool Pipeline::Validate(BuilderState* state)
{
  // TODO: Work out input/output types
  return true;
}

Node* Pipeline::GetChildInputNode()
{
  assert(!m_children.empty());
  return m_children.front()->GetChildInputNode();
}

Node* Pipeline::GetChildOutputNode()
{
  assert(!m_children.empty());
  return m_children.back()->GetChildOutputNode();
}

SplitJoin::SplitJoin(const std::string& name) : Node(name, nullptr, nullptr)
{
}

bool SplitJoin::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool SplitJoin::ConnectFrom(BuilderState* state, Node* src)
{
  if (!m_split_node || !m_join_node)
    return false;

  if (!m_split_node->ConnectFrom(state, src->GetChildOutputNode()))
    return false;

  return true;
}

bool SplitJoin::ConnectTo(BuilderState* state, Node* dst)
{
  if (!m_split_node || !m_join_node)
    return false;

  if (!m_join_node->ConnectTo(state, dst->GetChildInputNode()))
    return false;

  return true;
}

bool SplitJoin::AddChild(BuilderState* state, Node* node)
{
  if (IsStream(node))
  {
    if (!m_split_node)
    {
      state->Error("Splitjoin has not split yet");
      return false;
    }
    if (m_join_node)
    {
      state->Error("Splitjoin has not joined yet");
      return false;
    }

    if (!m_split_node->ConnectTo(state, node->GetChildInputNode()) ||
        !node->GetChildInputNode()->ConnectFrom(state, m_split_node->GetChildOutputNode()))
      return false;

    m_children.push_back(node);
    return true;
  }

  Split* split = dynamic_cast<Split*>(node);
  if (split)
  {
    if (m_split_node)
    {
      state->Error("Split already defined");
      return false;
    }

    m_split_node = split;
    return true;
  }

  Join* join = dynamic_cast<Join*>(node);
  if (join)
  {
    if (m_join_node)
    {
      state->Error("Join already defined");
      return false;
    }

    m_join_node = join;

    // Connect the last of the filters, or the split
    if (m_children.empty())
    {
      // Can we have an empty splitjoin?
      if (!m_split_node->ConnectTo(state, m_join_node))
        return false;

      return true;
    }
    else
    {
      for (Node* child_node : m_children)
      {
        if (!child_node->GetChildOutputNode()->ConnectTo(state, m_join_node) ||
            !m_join_node->ConnectFrom(state, child_node->GetChildOutputNode()))
          return false;
      }

      return true;
    }
  }

  assert(0 && "unknown node type");
  return false;
}

bool SplitJoin::Validate(BuilderState* state)
{
  if (!m_split_node)
  {
    state->Error("Splitjoin is missing split statement");
    return false;
  }

  if (!m_join_node)
  {
    state->Error("Splitjoin is missing join statement");
    return false;
  }

  return true;
}

Node* SplitJoin::GetChildInputNode()
{
  assert(m_split_node != nullptr);
  return m_split_node;
}

Node* SplitJoin::GetChildOutputNode()
{
  assert(m_join_node != nullptr);
  return m_join_node;
}

Split::Split(const std::string& name) : Node(name, nullptr, nullptr)
{
}

bool Split::AddChild(BuilderState* state, Node* node)
{
  state->Error("AddChild called for Split");
  return false;
}

bool Split::ConnectFrom(BuilderState* state, Node* src)
{
  AddInput(src);
  return true;
}

bool Split::ConnectTo(BuilderState* state, Node* dst)
{
  AddOutput(dst);
  return true;
}

bool Split::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool Split::Validate(BuilderState* state)
{
  // TODO: Work out input/output types
  return true;
}

Node* Split::GetChildInputNode()
{
  return this;
}

Node* Split::GetChildOutputNode()
{
  return this;
}

Join::Join(const std::string& name) : Node(name, nullptr, nullptr)
{
}

bool Join::AddChild(BuilderState* state, Node* node)
{
  state->Error("AddChild called for Split");
  return false;
}

bool Join::ConnectFrom(BuilderState* state, Node* src)
{
  AddInput(src);
  return true;
}

bool Join::ConnectTo(BuilderState* state, Node* dst)
{
  AddOutput(dst);
  return true;
}

bool Join::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool Join::Validate(BuilderState* state)
{
  // TODO: Work out input/output types
  return true;
}

Node* Join::GetChildInputNode()
{
  return this;
}

Node* Join::GetChildOutputNode()
{
  return this;
}
}