#include "hlstarget/component_generator.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "hlstarget/vhdl_helpers.h"
#include "llvm/Support/raw_ostream.h"
#include "parser/ast.h"
Log_SetChannel(HLSTarget::ComponentGenerator);

namespace HLSTarget
{
ComponentGenerator::ComponentGenerator(WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
                                       const std::string& module_name, llvm::raw_fd_ostream& os)
  : m_context(context), m_streamgraph(streamgraph), m_module_name(module_name), m_os(os)
{
}

ComponentGenerator::~ComponentGenerator()
{
}

bool ComponentGenerator::GenerateComponent()
{
  WriteHeader();
  // WriteFIFOComponentDeclaration();

  // Write filter permutations
  // for (const StreamGraph::FilterPermutation* filter_perm : m_streamgraph->GetFilterPermutationList())
  // WriteFilterPermutation(filter_perm);

  WriteGlobalSignals();

  if (!m_streamgraph->GetRootNode()->Accept(this))
  {
    Log_ErrorPrintf("Stream graph walk failed");
    return false;
  }

  m_os << "-- Signal declarations\n";
  m_os << m_signals.str();
  m_os << "\n";

  m_os << "-- Start instantiating components\n";
  m_os << "begin\n";
  m_os << "\n";
  m_os << m_body.str();
  m_os << "\n";

  WriteFooter();
  return true;
}

void ComponentGenerator::WriteHeader()
{
  m_os << "library IEEE;\n";
  m_os << "use IEEE.STD_LOGIC_1164.ALL;\n";
  m_os << "use IEEE.NUMERIC_STD.ALL;\n";
  m_os << "\n";

  m_os << "entity " << m_module_name << " is\n";
  m_os << "  port (\n";

  const Type* program_input_type = m_streamgraph->GetProgramInputType();
  const Type* program_output_type = m_streamgraph->GetProgramOutputType();
  if (!program_input_type->IsVoid())
  {
    m_os << "    prog_din : in " << VHDLHelpers::GetVHDLBitVectorType(program_input_type) << ";\n";
    m_os << "    prog_empty_n : in std_logic;\n";
    m_os << "    prog_read : out std_logic;\n";
  }
  if (!program_output_type->IsVoid())
  {
    m_os << "    prog_dout : out " << VHDLHelpers::GetVHDLBitVectorType(program_output_type) << ";\n";
    m_os << "    prog_full_n : in std_logic;\n";
    m_os << "    prog_write : out std_logic;\n";
  }

  m_os << "    clk : in std_logic;\n";
  m_os << "    rst_n : in std_logic\n";
  m_os << "  );\n";
  m_os << "end " << m_module_name << ";\n";
  m_os << "\n";

  m_os << "architecture behav of " << m_module_name << " is\n";
  m_os << "\n";
}

void ComponentGenerator::WriteFooter()
{
  m_os << "end behav;\n";
}

void ComponentGenerator::WriteGlobalSignals()
{
}

void ComponentGenerator::WriteFIFOComponentDeclaration()
{
  m_os << "-- FIFO queue component declaration\n";
  m_os << "component " << VHDLHelpers::FIFO_COMPONENT_NAME << " is\n";
  m_os << "  generic (\n";
  m_os << "    constant DATA_WIDTH : positive := 8;\n";
  m_os << "    constant SIZE : positive := 16;\n";
  m_os << "  );\n";
  m_os << "  port (\n";
  m_os << "    clk : in std_logic;\n";
  m_os << "    rst_n : in std_logic;\n";
  m_os << "    read : in std_logic;\n";
  m_os << "    write : in std_logic;\n";
  m_os << "    empty_n : out std_logic;\n";
  m_os << "    full_n : out std_logic;\n";
  m_os << "    dout : out std_logic_vector(DATA_WIDTH - 1 downto 0);\n";
  m_os << "    din : out std_logic_vector(DATA_WIDTH - 1 downto 0)\n";
  m_os << "  );\n";
  m_os << "end component;\n";
  m_os << "\n";
}

void ComponentGenerator::WriteFilterPermutation(const StreamGraph::FilterPermutation* filter)
{
  std::string component_name = StringFromFormat("filter_%s", filter->GetName().c_str());
  m_os << "-- " << component_name << " (from filter " << filter->GetFilterDeclaration()->GetName() << ")\n";
  m_os << "component " << component_name << " is\n";
  m_os << "  port (\n";
  m_os << "    ap_clk : in std_logic;\n";
  m_os << "    ap_rst_n : in std_logic";
  if (!filter->GetInputType()->IsVoid())
  {
    m_os << ";\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_dout : in "
         << VHDLHelpers::GetVHDLBitVectorType(filter->GetInputType()) << ";\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_empty_n : in std_logic;\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_read : out std_logic";
  }

  if (!filter->GetOutputType()->IsVoid())
  {
    m_os << ";\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_dout : out "
         << VHDLHelpers::GetVHDLBitVectorType(filter->GetOutputType()) << ";\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_full_n : in std_logic;\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_write : out std_logic";
  }

  m_os << "\n";
  m_os << "  );\n";
  m_os << "end component;\n";
  m_os << "\n";
}

bool ComponentGenerator::Visit(StreamGraph::Filter* node)
{
  const std::string& name = node->GetName();
  m_body << "-- Filter instance " << name << " (filter " << node->GetFilterPermutation()->GetName() << ")\n";

  // Input FIFO queue
  if (!node->GetInputType()->IsVoid())
  {
    m_signals << "signal " << name << "_fifo_read : std_logic;\n";
    m_signals << "signal " << name << "_fifo_write : std_logic;\n";
    m_signals << "signal " << name << "_fifo_empty_n : std_logic;\n";
    m_signals << "signal " << name << "_fifo_full_n : std_logic;\n";
    m_signals << "signal " << name << "_fifo_dout : " << VHDLHelpers::GetVHDLBitVectorType(node->GetInputType())
              << ";\n";
    m_signals << "signal " << name << "_fifo_din : " << VHDLHelpers::GetVHDLBitVectorType(node->GetInputType())
              << ";\n";

    u32 fifo_depth = std::max(node->GetNetPeek(), node->GetNetPop()) * 16;
    m_body << "-- FIFO with depth " << fifo_depth << "\n";
    // m_body << name << "_fifo : " << FIFO_COMPONENT_NAME << "\n";
    m_body << name << "_fifo : entity work." << VHDLHelpers::FIFO_COMPONENT_NAME << "(behav)\n";
    m_body << "  generic map (\n";
    m_body << "    DATA_WIDTH => " << VHDLHelpers::GetBitWidthForType(node->GetInputType()) << ",\n";
    m_body << "    SIZE => " << fifo_depth << "\n";
    m_body << "  )\n";
    m_body << "  port map (\n";
    m_body << "    clk => clk,\n";
    m_body << "    rst_n => rst_n,\n";
    m_body << "    read => " << name << "_fifo_read,\n";
    m_body << "    write => " << name << "_fifo_write,\n";
    m_body << "    empty_n => " << name << "_fifo_empty_n,\n";
    m_body << "    full_n => " << name << "_fifo_full_n,\n";
    m_body << "    dout => " << name << "_fifo_dout,\n";
    m_body << "    din => " << name << "_fifo_din\n";
    m_body << "  );\n";
    m_body << "\n";
  }

  // Component instantiation
  // m_body << name << " : filter_" << node->GetFilterPermutation()->GetName() << "\n";
  m_body << name << " : entity work.filter_" << node->GetFilterPermutation()->GetName() << "(behav)\n";
  m_body << "  port map (\n";
  m_body << "    ap_clk => clk,\n";
  m_body << "    ap_rst_n => rst_n";
  if (!node->GetInputType()->IsVoid())
  {
    m_body << ",\n";
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_dout => " << name << "_fifo_dout,\n";
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_read => " << name << "_fifo_read,\n";
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_empty_n => " << name << "_fifo_empty_n";
  }
  if (!node->GetOutputType()->IsVoid())
  {
    const std::string& output_name = node->GetOutputChannelName();
    if (!output_name.empty())
    {
      m_body << ",\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_din => " << output_name << "_fifo_din,\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_write => " << output_name << "_fifo_write,\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_full_n => " << output_name << "_fifo_full_n";
    }
    else
    {
      // This is the last filter in the program.
      m_body << ",\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_din => prog_dout,\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_write => prog_write,\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_full_n => prog_full_n";
    }
  }
  m_body << "\n";
  m_body << "  );\n";
  m_body << "\n";

  return true;
}

bool ComponentGenerator::Visit(StreamGraph::Pipeline* node)
{
  // Visit all nodes in the pipeline, and write as it goes.
  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

bool ComponentGenerator::Visit(StreamGraph::SplitJoin* node)
{
  return true;
}

bool ComponentGenerator::Visit(StreamGraph::Split* node)
{
  return true;
}

bool ComponentGenerator::Visit(StreamGraph::Join* node)
{
  return true;
}

} // namespace HLSTarget
