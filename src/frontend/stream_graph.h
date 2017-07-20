#pragma once
#include <string>
#include <vector>

namespace StreamGraph
{
class Node
{
public:
};

class FilterNode : public Node
{
public:
  FilterNode(const std::string& filter_name) : m_filter_name(filter_name) {}
  ~FilterNode() = default;

private:
  std::string m_filter_name;
};

class SplitJoinNode : public Node
{
public:
  SplitJoinNode();
  ~SplitJoinNode();

  void AddChild(Node* node) { m_children.push_back(node); }

private:
  std::vector<Node*> m_children;
};
}
