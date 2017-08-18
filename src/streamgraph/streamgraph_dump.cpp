#include <cassert>
#include <cstdarg>
#include <sstream>
#include "common/string_helpers.h"
#include "streamgraph/streamgraph.h"

namespace StreamGraph
{
class StreamGraphDumpVisitor : public Visitor
{
public:
  StreamGraphDumpVisitor() = default;
  ~StreamGraphDumpVisitor() = default;

  std::string ToString() const { return m_out.str(); }

  bool Visit(Filter* node) override;
  bool Visit(Pipeline* node) override;
  bool Visit(SplitJoin* node) override;
  bool Visit(Split* node) override;
  bool Visit(Join* node) override;

  void Write(const char* fmt, ...);
  void WriteLine(const char* fmt, ...);
  void Indent();
  void Deindent();
  void WriteEdge(const Node* src, const Node* dst);

protected:
  std::stringstream m_out;
  unsigned int m_indent = 0;
};

void StreamGraphDumpVisitor::Write(const char* fmt, ...)
{
  for (unsigned int i = 0; i < m_indent; i++)
    m_out << "  ";

  va_list ap;
  va_start(ap, fmt);
  m_out << StringFromFormatV(fmt, ap);
  va_end(ap);

  m_out << std::endl;
}

void StreamGraphDumpVisitor::WriteLine(const char* fmt, ...)
{
  for (unsigned int i = 0; i < m_indent; i++)
    m_out << "  ";

  va_list ap;
  va_start(ap, fmt);
  m_out << StringFromFormatV(fmt, ap);
  va_end(ap);

  m_out << std::endl;
}

void StreamGraphDumpVisitor::Indent()
{
  m_indent++;
}

void StreamGraphDumpVisitor::Deindent()
{
  assert(m_indent > 0);
  m_indent--;
}

void StreamGraphDumpVisitor::WriteEdge(const Node* src, const Node* dst)
{
  WriteLine("%s -> %s;", src->GetName().c_str(), dst->GetName().c_str());
}

bool StreamGraphDumpVisitor::Visit(Filter* node)
{
  WriteLine("%s [shape=ellipse];", node->GetName().c_str());
  WriteLine("# %s peek %u(%u) pop %u(%u) push %u(%u) mult %u", node->GetName().c_str(), node->GetPeekRate(),
            node->GetNetPeek(), node->GetPopRate(), node->GetNetPop(), node->GetPushRate(), node->GetNetPush(),
            node->GetMultiplicity());

  if (node->HasOutputConnection())
    WriteEdge(node, node->GetOutputConnection());

  return true;
}

bool StreamGraphDumpVisitor::Visit(Pipeline* node)
{
  if (m_indent > 0)
    WriteLine("subgraph cluster_%s {", node->GetName().c_str());
  Indent();

  WriteLine("label = \"%s\";\n", node->GetName().c_str());
  WriteLine("# %s peek %u(%u) pop %u(%u) push %u(%u) mult %u", node->GetName().c_str(), node->GetPeekRate(),
            node->GetNetPeek(), node->GetPopRate(), node->GetNetPop(), node->GetPushRate(), node->GetNetPush(),
            node->GetMultiplicity());

  for (Node* child : node->GetChildren())
    child->Accept(this);

  Deindent();

  if (m_indent > 0)
    WriteLine("}");

  return true;
}

bool StreamGraphDumpVisitor::Visit(SplitJoin* node)
{
  WriteLine("subgraph cluster_%s {", node->GetName().c_str());
  Indent();

  WriteLine("label = \"%s\";\n", node->GetName().c_str());
  WriteLine("# %s peek %u(%u) pop %u(%u) push %u(%u) mult %u", node->GetName().c_str(), node->GetPeekRate(),
            node->GetNetPeek(), node->GetPopRate(), node->GetNetPop(), node->GetPushRate(), node->GetNetPush(),
            node->GetMultiplicity());

  node->GetSplitNode()->Accept(this);
  node->GetJoinNode()->Accept(this);

  for (Node* child : node->GetChildren())
    child->Accept(this);

  Deindent();
  WriteLine("}");

  return true;
}

bool StreamGraphDumpVisitor::Visit(Split* node)
{
  WriteLine("%s [shape=triangle];", node->GetName().c_str());
  WriteLine("# %s peek %u(%u) pop %u(%u) push %u(%u) mult %u", node->GetName().c_str(), node->GetPeekRate(),
            node->GetNetPeek(), node->GetPopRate(), node->GetNetPop(), node->GetPushRate(), node->GetNetPush(),
            node->GetMultiplicity());

  for (const Node* out_node : node->GetOutputs())
    WriteEdge(node, out_node);

  return true;
}

bool StreamGraphDumpVisitor::Visit(Join* node)
{
  WriteLine("%s [shape=invtriangle];", node->GetName().c_str());
  WriteLine("# %s peek %u(%u) pop %u(%u) push %u(%u) mult %u", node->GetName().c_str(), node->GetPeekRate(),
            node->GetNetPeek(), node->GetPopRate(), node->GetNetPop(), node->GetPushRate(), node->GetNetPush(),
            node->GetMultiplicity());

  if (node->HasOutputConnection())
    WriteEdge(node, node->GetOutputConnection());

  return true;
}

std::string StreamGraph::Dump()
{
  StreamGraphDumpVisitor visitor;
  visitor.WriteLine("digraph G {");
  m_root_node->Accept(&visitor);
  visitor.WriteLine("}");
  return visitor.ToString();
}
}