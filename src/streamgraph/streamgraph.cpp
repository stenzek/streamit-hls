#include "streamgraph/streamgraph.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include "common/log.h"
#include "common/string_helpers.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph_builder.h"
Log_SetChannel(StreamGraph);

namespace StreamGraph
{
static bool IsStream(Node* node)
{
  return (dynamic_cast<Filter*>(node) != nullptr || dynamic_cast<Pipeline*>(node) != nullptr ||
          dynamic_cast<SplitJoin*>(node) != nullptr);
}

StreamGraph::StreamGraph(Node* root, const FilterPermutationList& filter_permutation_list)
  : m_root_node(root), m_filter_permutations(filter_permutation_list)
{
}

StreamGraph::~StreamGraph()
{
  delete m_root_node;
}

StreamGraph::FilterInstanceList StreamGraph::GetFilterInstanceList() const
{
  struct TheVisitor : Visitor
  {
    FilterInstanceList& m_list;

    TheVisitor(FilterInstanceList& list_) : m_list(list_) {}

    bool Visit(Filter* node) override final
    {
      m_list.push_back(node);
      return true;
    }

    virtual bool Visit(Pipeline* node) override
    {
      for (Node* child : node->GetChildren())
        child->Accept(this);

      return true;
    }

    virtual bool Visit(SplitJoin* node) override
    {
      for (Node* child : node->GetChildren())
        child->Accept(this);

      return true;
    }

    virtual bool Visit(Split* node) override { return true; }

    virtual bool Visit(Join* node) override { return true; }
  };

  FilterInstanceList list;
  TheVisitor visitor(list);
  m_root_node->Accept(&visitor);
  return list;
}

llvm::Type* StreamGraph::GetProgramInputType() const
{
  return m_root_node->GetInputType();
}

llvm::Type* StreamGraph::GetProgramOutputType() const
{
  return m_root_node->GetOutputType();
}

void StreamGraph::WidenChannels()
{
  m_root_node->WidenChannels();

  // Create new filter instances for those which are widened.
  FilterInstanceList filters = GetFilterInstanceList();
  for (Filter* filter : filters)
  {
    if (filter->GetInputChannelWidth() != filter->GetFilterPermutation()->GetInputChannelWidth() ||
        filter->GetOutputChannelWidth() != filter->GetFilterPermutation()->GetOutputChannelWidth())
    {
      // Find an existing permutation which matches.
      auto perm = std::find_if(m_filter_permutations.begin(), m_filter_permutations.end(),
                               [filter](const FilterPermutation* perm) {
                                 return (perm->GetInputChannelWidth() == filter->GetInputChannelWidth() &&
                                         perm->GetOutputChannelWidth() == filter->GetOutputChannelWidth());
                               });

      if (perm != m_filter_permutations.end())
      {
        filter->m_filter_permutation = *perm;
        continue;
      }

      // Not found? Create a new one.
      const FilterPermutation* existing_perm = filter->GetFilterPermutation();
      std::string name = StringFromFormat("%s_%u", existing_perm->GetFilterDeclaration()->GetName().c_str(),
                                          unsigned(m_filter_permutations.size() + 1));
      FilterPermutation* new_perm =
        new FilterPermutation(name, existing_perm->GetFilterDeclaration(), existing_perm->GetFilterParameters(),
                              existing_perm->GetInputType(), existing_perm->GetOutputType(),
                              existing_perm->GetPeekRate(), existing_perm->GetPopRate(), existing_perm->GetPushRate(),
                              filter->GetInputChannelWidth(), filter->GetOutputChannelWidth());
      filter->m_filter_permutation = new_perm;
      m_filter_permutations.push_back(new_perm);
      Log_DevPrintf("Created new permutation of %s with i/o channel widths (%u/%u) -> %s",
                    existing_perm->GetName().c_str(), filter->GetInputChannelWidth(), filter->GetOutputChannelWidth(),
                    new_perm->GetName().c_str());
    }
  }

  // Remove now-unused filter permutations.
  for (size_t i = 0; i < m_filter_permutations.size();)
  {
    FilterPermutation* perm = m_filter_permutations[i];
    if (std::none_of(filters.begin(), filters.end(),
                     [perm](const Filter* filter) { return (filter->GetFilterPermutation() == perm); }))
    {
      Log_DevPrintf("Removing unused permutation %s", perm->GetName().c_str());
      m_filter_permutations.erase(m_filter_permutations.begin() + i);
      delete perm;
    }
    else
    {
      i++;
    }
  }
}

void FilterParameters::AddParameter(const AST::ParameterDeclaration* decl, const void* data, size_t data_len,
                                    llvm::Constant* value)
{
  Parameter p;
  p.decl = decl;
  p.data_offset = m_data.size();
  p.data_length = data_len;
  p.value = value;

  if (data_len > 0)
  {
    m_data.resize(m_data.size() + data_len);
    std::memcpy(&m_data[p.data_offset], data, data_len);
  }

  m_params.emplace_back(std::move(p));
}

bool FilterParameters::operator==(const FilterParameters& rhs) const
{
  return (m_data == rhs.m_data);
}

bool FilterParameters::operator!=(const FilterParameters& rhs) const
{
  return (m_data != rhs.m_data);
}

FilterPermutation::FilterPermutation(const std::string& name, const AST::FilterDeclaration* filter_decl,
                                     const FilterParameters& filter_params, llvm::Type* input_type,
                                     llvm::Type* output_type, int peek_rate, int pop_rate, int push_rate,
                                     u32 input_channel_width, u32 output_channel_width)
  : m_name(name), m_filter_decl(filter_decl), m_filter_params(filter_params), m_input_type(input_type),
    m_output_type(output_type), m_peek_rate(peek_rate), m_pop_rate(pop_rate), m_push_rate(push_rate),
    m_input_channel_width(input_channel_width), m_output_channel_width(output_channel_width)
{
}

bool FilterPermutation::IsBuiltin() const
{
  return m_filter_decl->IsBuiltin();
}

Node::Node(const std::string& name, llvm::Type* input_type, llvm::Type* output_type)
  : m_name(name), m_input_type(input_type), m_output_type(output_type)
{
}

Filter::Filter(const std::string& instance_name, const FilterPermutation* filter)
  : Node(instance_name, filter->GetInputType(), filter->GetOutputType()), m_filter_permutation(filter)
{
  m_peek_rate = static_cast<u32>(m_filter_permutation->GetPeekRate());
  m_pop_rate = static_cast<u32>(m_filter_permutation->GetPopRate());
  m_push_rate = static_cast<u32>(m_filter_permutation->GetPushRate());
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

void Filter::SetInputChannelWidth(u32 width)
{
  m_input_channel_width = width;
}

void Filter::WidenChannels()
{
  if (!m_output_connection)
    return;

  u32 width = GetPushRate();
  if (width <= 1 || width != m_output_connection->GetPopRate())
    return;

  // Currently not supported when peeking more than popping.
  if (m_output_connection->GetPeekRate() > 0 && m_output_connection->GetPeekRate() > m_output_connection->GetPopRate())
    return;

  Log_DevPrintf("Widening channel between %s and %s to %u", m_name.c_str(), m_output_connection->GetName().c_str(),
                width);
  m_output_channel_width = width;
  m_output_connection->SetInputChannelWidth(width);
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

void Pipeline::SetInputChannelWidth(u32 width)
{
  assert(0 && "should not be called");
}

void Pipeline::WidenChannels()
{
  for (Node* child : m_children)
    child->WidenChannels();
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
    u32 prev_send = m_split_node->GetNetPush() * u32(m_split_node->GetDistribution().at(i));
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

  // the join one is the one that determines how many times it needs to run, really...
  // TODO: Matching against the first will break with variable distributions...
  Node* first_child = m_children.front();
  u32 prev_send = first_child->GetNetPush();
  u32 next_recv = m_join_node->GetNetPop() * u32(m_join_node->GetDistribution().at(0));
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
      // first child - input type because this is the filter *in* the splitjoin
      m_split_node->SetDataType(node->GetInputType());
      m_input_type = node->GetInputType();
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
    if (!m_split_node)
    {
      state->Error("Split not defined");
      return false;
    }

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
      m_output_type = m_children.back()->GetOutputType();
      for (Node* child_node : m_children)
      {
        m_join_node->AddIncomingStream();
        if (!child_node->ConnectTo(state, m_join_node))
          return false;
      }

      // For roundrobin splits
      if (m_split_node->GetMode() == Split::Mode::Roundrobin)
      {
        if (m_split_node->GetDistribution().size() == 1)
        {
          // Set distribution of all to the first
          m_split_node->m_pop_rate += m_split_node->GetDistribution()[0];
          for (size_t i = 1; i < m_children.size(); i++)
          {
            m_split_node->GetDistribution().push_back(m_split_node->GetDistribution()[0]);
            m_split_node->m_pop_rate += m_split_node->GetDistribution()[0];
          }
        }
        else if (m_split_node->GetDistribution().size() == 0)
        {
          // Set distribution for all to one
          for (size_t i = 0; i < m_children.size(); i++)
          {
            m_split_node->GetDistribution().push_back(1);
            m_split_node->m_pop_rate++;
          }
        }
        else
        {
          for (size_t i = 0; i < m_children.size(); i++)
            m_split_node->m_pop_rate += m_split_node->GetDistribution()[i];
        }
      }
      else
      {
        // Set distribution for all to one for duplicate
        m_split_node->m_pop_rate = 1;
        for (size_t i = 0; i < m_children.size(); i++)
          m_split_node->GetDistribution().push_back(1);
      }

      // For roundrobin joins
      if (m_join_node->GetDistribution().size() == 1)
      {
        // Set distribution of all to the first
        m_join_node->m_push_rate += m_join_node->GetDistribution()[0];
        for (size_t i = 1; i < m_children.size(); i++)
        {
          m_join_node->GetDistribution().push_back(m_join_node->GetDistribution()[0]);
          m_join_node->m_push_rate += m_join_node->GetDistribution()[0];
        }
      }
      else if (m_join_node->GetDistribution().size() == 0)
      {
        // Set distribution for all to one
        for (size_t i = 0; i < m_children.size(); i++)
        {
          m_join_node->GetDistribution().push_back(1);
          m_join_node->m_push_rate++;
        }
      }
      else
      {
        for (size_t i = 0; i < m_children.size(); i++)
          m_join_node->m_push_rate += m_join_node->GetDistribution()[i];
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

  // For roundrobin splits, we either have to have a distribution of size zero or one, or numchildren == distribution
  if (m_split_node->GetMode() == Split::Mode::Roundrobin && m_split_node->GetDistribution().size() > 1 &&
      m_children.size() != m_split_node->GetDistribution().size())
  {
    state->Error("Fixed distributions must match the number of filters.");
    return false;
  }

  // Same for roundrobin joins
  if (m_join_node->GetDistribution().size() > 1 && m_children.size() != m_join_node->GetDistribution().size())
  {
    state->Error("Fixed distributions must match the number of filters.");
    return false;
  }

  return result;
}

void SplitJoin::SetInputChannelWidth(u32 width)
{
  assert(0 && "should not be called");
}

void SplitJoin::WidenChannels()
{
  m_split_node->WidenChannels();

  // split has a push rate of 1, join has a pop rate of 1, so this shouldn't break anything.
  for (Node* child : m_children)
    m_split_node->WidenChannels();

  m_join_node->WidenChannels();
}

Split::Split(const std::string& name, Mode mode, const std::vector<int>& distribution)
  : Node(name, nullptr, nullptr), m_mode(mode), m_distribution(distribution)
{
  // pushing one to every filter, popping one if roundrobin, otherwise ndests
  m_peek_rate = 0;
  m_push_rate = 1;
  m_pop_rate = 0;
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

void Split::SetDataType(llvm::Type* type)
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

void Split::SetInputChannelWidth(u32 width)
{
  m_input_channel_width = width;
}

void Split::WidenChannels()
{
  // We can't widen duplicate splits.
  // In our targets we already do duplicate in a single "operation" anyway.
  if (m_mode != Split::Mode::Roundrobin)
    return;

  // To be able to widen the channel from split to children, we have to be able
  // to widen it for all the children.
  for (size_t idx = 0; idx < m_outputs.size(); idx++)
  {
    if (m_distribution[idx] != m_outputs[idx]->GetPopRate())
      return;
  }

  // Set input channel widths to distributions.
  for (size_t idx = 0; idx < m_outputs.size(); idx++)
    m_outputs[idx]->SetInputChannelWidth(m_distribution[idx]);
}

Join::Join(const std::string& name, const std::vector<int>& distribution)
  : Node(name, nullptr, nullptr), m_distribution(distribution)
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
  // m_push_rate++;
}

void Join::SetDataType(llvm::Type* type)
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

void Join::SetInputChannelWidth(u32 width)
{
  assert(0 && "should not be called");
}

void Join::WidenChannels()
{
  if (!m_output_connection)
    return;

  u32 width = GetPushRate();
  if (width <= 1 || width != m_output_connection->GetPopRate())
    return;

  Log_DevPrintf("Widening channel between %s and %s to %u", m_name.c_str(), m_output_connection->GetName().c_str(),
                width);
  m_output_channel_width = width;
  m_output_connection->SetInputChannelWidth(width);
}
}