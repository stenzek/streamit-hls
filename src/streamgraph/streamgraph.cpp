#include "streamgraph/streamgraph.h"
#include <cassert>
#include "common/string_helpers.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph_builder.h"

namespace StreamGraph
{
static bool IsStream(Node* node)
{
  return (dynamic_cast<Filter*>(node) != nullptr || dynamic_cast<Pipeline*>(node) != nullptr ||
          dynamic_cast<SplitJoin*>(node) != nullptr);
}

StreamGraph::StreamGraph(Node* root) : m_root_node(root)
{
}

StreamGraph::~StreamGraph()
{
  delete m_root_node;
}

Node::Node(const std::string& name, const Type* input_type, const Type* output_type)
  : m_name(name), m_input_type(input_type), m_output_type(output_type)
{
}

Filter::Filter(AST::FilterDeclaration* decl, const std::string& name)
  : Node(name, decl->GetInputType(), decl->GetOutputType()), m_filter_decl(decl)
{
  // Filter has to have a work block (this should already be validated at AST time)
  assert(m_filter_decl->HasWorkBlock());
  m_peek_rate = m_filter_decl->GetWorkBlock()->GetPeekRate();
  m_pop_rate = m_filter_decl->GetWorkBlock()->GetPopRate();
  m_push_rate = m_filter_decl->GetWorkBlock()->GetPushRate();
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

bool Filter::ConnectTo(BuilderState* state, Node* dst)
{
  if (m_output_connection)
  {
    state->Error("Filter %s already has an output connection", m_name.c_str());
    return false;
  }

  m_output_connection = dst->GetInputNode();
  m_output_channel_name = dst->GetInputChannelName();
  return true;
}

Node* Filter::GetInputNode()
{
  return this;
}

std::string Filter::GetInputChannelName()
{
  return m_name;
}

void Filter::SteadySchedule()
{
}

void Filter::AddMultiplicity(u32 count)
{
  m_multiplicity *= count;
}

bool Filter::Validate(BuilderState* state)
{
  return true;
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

  if (m_children.empty())
  {
    // first child
    m_input_type = node->GetInputType();
  }
  else
  {
    // not first child
    Node* back = m_children.back();
    if (!back->ConnectTo(state, node))
      return false;
  }

  m_output_type = node->GetOutputType();
  m_children.push_back(node);
  return true;
}

bool Pipeline::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool Pipeline::ConnectTo(BuilderState* state, Node* node)
{
  if (m_children.empty())
  {
    state->Error("Connecting empty pipeline to sink.");
    return false;
  }

  Node* back = m_children.back();
  if (!back->ConnectTo(state, node))
    return false;

  return true;
}

bool Pipeline::Validate(BuilderState* state)
{
  // TODO: Work out input/output types
  if (m_children.empty())
  {
    state->Error("Pipeline is missing children");
    return false;
  }

  bool result = true;
  for (Node* node : m_children)
    result &= node->Validate(state);

  return result;
}

Node* Pipeline::GetInputNode()
{
  assert(!m_children.empty());
  return m_children.front()->GetInputNode();
}

std::string Pipeline::GetInputChannelName()
{
  assert(!m_children.empty());
  return m_children.front()->GetInputChannelName();
}

void Pipeline::SteadySchedule()
{
  for (Node* child : m_children)
    child->SteadySchedule();

  Node* prev_child = m_children.front();
  for (size_t i = 1; i < m_children.size(); i++)
  {
    Node* child = m_children[i];
    u32 prev_send = prev_child->GetNetPush();
    u32 next_recv = child->GetNetPop();
    if (prev_send != next_recv)
    {
      u32 gcd1 = gcd(prev_send, next_recv);
      prev_send /= gcd1;
      next_recv /= gcd1;

      // Multiply previous children output to handle the new request size
      for (size_t j = 0; j < i; j++)
        m_children[j]->AddMultiplicity(next_recv);

      child->AddMultiplicity(prev_send);
    }

    prev_child = child;
  }

  Node* first_child = m_children.front();
  Node* last_child = m_children.back();
  m_peek_rate = first_child->GetNetPeek();
  m_pop_rate = first_child->GetNetPop();
  m_push_rate = last_child->GetNetPush();
}

void Pipeline::AddMultiplicity(u32 count)
{
  m_multiplicity *= count;
  for (Node* child : m_children)
    child->AddMultiplicity(count);
}

SplitJoin::SplitJoin(const std::string& name) : Node(name, nullptr, nullptr)
{
}

bool SplitJoin::Accept(Visitor* visitor)
{
  return visitor->Visit(this);
}

bool SplitJoin::ConnectTo(BuilderState* state, Node* dst)
{
  assert(m_join_node != nullptr);
  return m_join_node->ConnectTo(state, dst);
}

Node* SplitJoin::GetInputNode()
{
  assert(m_split_node != nullptr);
  return m_split_node;
}

std::string SplitJoin::GetInputChannelName()
{
  assert(m_split_node != nullptr);
  return m_split_node->GetInputChannelName();
}

void SplitJoin::SteadySchedule()
{
  const Node* child_parent = m_split_node;
  for (Node* child : m_children)
    child->SteadySchedule();

  // TODO: Fix this up for roundrobin without even distribution
  Node* prev_child = m_children.front();
  for (size_t i = 0; i < m_children.size(); i++)
  {
    Node* next = m_children[i];
    u32 prev_send = m_split_node->GetNetPush();
    u32 next_recv = next->GetNetPop();
    if (prev_send != next_recv)
    {
      u32 gcd1 = gcd(prev_send, next_recv);
      prev_send /= gcd1;
      next_recv /= gcd1;

      // Multiply previous children output to handle the new request size
      m_split_node->AddMultiplicity(next_recv);
      for (size_t j = 0; j < i; j++)
        m_children[j]->AddMultiplicity(next_recv);
      next->AddMultiplicity(prev_send);
    }
  }

  Node* first_child = m_children.front();
  u32 prev_send = first_child->GetNetPush();
  u32 next_recv = m_join_node->GetNetPop();
  if (prev_send != next_recv)
  {
    u32 gcd1 = gcd(prev_send, next_recv);
    prev_send /= gcd1;
    next_recv /= gcd1;

    // Multiply previous children output to handle the new request size
    m_split_node->AddMultiplicity(next_recv);
    for (Node* child : m_children)
      child->AddMultiplicity(next_recv);
    m_join_node->AddMultiplicity(prev_send);
  }

  m_peek_rate = m_split_node->GetNetPeek();
  m_pop_rate = m_split_node->GetNetPop();
  m_push_rate = m_join_node->GetNetPush();
}

void SplitJoin::AddMultiplicity(u32 count)
{
  m_multiplicity *= count;
  m_split_node->AddMultiplicity(count);
  m_join_node->AddMultiplicity(count);
  for (Node* child : m_children)
    child->AddMultiplicity(count);
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

    if (!m_split_node->ConnectTo(state, node))
      return false;

    if (m_children.empty())
    {
      // first child
      m_split_node->SetDataType(node->GetInputType());
    }

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
      // TODO: How does this work with data types..
      m_join_node->AddIncomingStream();
      if (!m_split_node->ConnectTo(state, m_join_node))
        return false;

      return true;
    }
    else
    {
      m_join_node->SetDataType(m_children.back()->GetOutputType());
      for (Node* child_node : m_children)
      {
        m_join_node->AddIncomingStream();
        if (!child_node->ConnectTo(state, m_join_node))
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

  // TODO: Check types all match

  bool result = true;
  result &= m_split_node->Validate(state);
  result &= m_join_node->Validate(state);
  for (Node* child : m_children)
    result &= child->Validate(state);
  return result;
}

Split::Split(const std::string& name) : Node(name, nullptr, nullptr)
{
  m_peek_rate = 0;
  m_pop_rate = 1;
  m_push_rate = 1;
}

bool Split::AddChild(BuilderState* state, Node* node)
{
  state->Error("AddChild called for Split");
  return false;
}

bool Split::ConnectTo(BuilderState* state, Node* dst)
{
  m_outputs.push_back(dst->GetInputNode());
  m_output_channel_names.push_back(dst->GetInputChannelName());
  return true;
}

Node* Split::GetInputNode()
{
  return this;
}

std::string Split::GetInputChannelName()
{
  return m_name;
}

void Split::SteadySchedule()
{
  assert(0 && "Should not be called");
}

void Split::AddMultiplicity(u32 count)
{
  m_multiplicity *= count;
}

void Split::SetDataType(const Type* type)
{
  m_input_type = type;
  m_output_type = type;
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

Join::Join(const std::string& name) : Node(name, nullptr, nullptr)
{
  m_peek_rate = 0;
  m_pop_rate = 1;
  m_push_rate = 0;
}

bool Join::AddChild(BuilderState* state, Node* node)
{
  state->Error("AddChild called for Split");
  return false;
}

bool Join::ConnectTo(BuilderState* state, Node* dst)
{
  if (m_output_connection)
  {
    state->Error("Join %s already has an output connection", m_name.c_str());
    return false;
  }

  m_output_connection = dst->GetInputNode();
  m_output_channel_name = dst->GetInputChannelName();
  return true;
}

Node* Join::GetInputNode()
{
  return this;
}

std::string Join::GetInputChannelName()
{
  return StringFromFormat("%s_%u", m_name.c_str(), m_incoming_streams);
}

void Join::AddIncomingStream()
{
  m_incoming_streams++;
  // m_pop_rate++;
  m_push_rate++;
}

void Join::SetDataType(const Type* type)
{
  m_input_type = type;
  m_output_type = type;
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

void Join::SteadySchedule()
{
  assert(0 && "Should not be called");
}

void Join::AddMultiplicity(u32 count)
{
  m_multiplicity *= count;
}
}