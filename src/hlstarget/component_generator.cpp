#include "hlstarget/component_generator.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "hlstarget/vhdl_helpers.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"
#include "parser/ast.h"
Log_SetChannel(HLSTarget::ComponentGenerator);

namespace HLSTarget
{
ComponentGenerator::ComponentGenerator(Frontend::WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
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

  const llvm::Type* program_input_type = m_streamgraph->GetProgramInputType();
  const llvm::Type* program_output_type = m_streamgraph->GetProgramOutputType();
  if (!program_input_type->isVoidTy())
  {
    m_os << "    prog_input_dout : in " << VHDLHelpers::GetVHDLBitVectorType(program_input_type) << ";\n";
    m_os << "    prog_input_empty_n : in std_logic;\n";
    m_os << "    prog_input_read : out std_logic;\n";
  }
  if (!program_output_type->isVoidTy())
  {
    m_os << "    prog_output_din : out " << VHDLHelpers::GetVHDLBitVectorType(program_output_type) << ";\n";
    m_os << "    prog_output_full_n : in std_logic;\n";
    m_os << "    prog_output_write : out std_logic;\n";
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
  if (!filter->GetInputType()->isVoidTy())
  {
    m_os << ";\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_dout : in "
         << VHDLHelpers::GetVHDLBitVectorType(filter->GetInputType()) << ";\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_empty_n : in std_logic;\n";
    m_os << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_read : out std_logic";
  }

  if (!filter->GetOutputType()->isVoidTy())
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

void ComponentGenerator::WriteFIFO(const std::string& name, u32 data_width, u32 depth)
{
  m_signals << "signal " << name << "_read : std_logic;\n";
  m_signals << "signal " << name << "_write : std_logic;\n";
  m_signals << "signal " << name << "_empty_n : std_logic;\n";
  m_signals << "signal " << name << "_full_n : std_logic;\n";
  m_signals << "signal " << name << "_dout : std_logic_vector(" << (data_width - 1) << " downto 0);\n";
  m_signals << "signal " << name << "_din : std_logic_vector(" << (data_width - 1) << " downto 0);\n";

  if (m_use_srl_fifos)
  {
    bool srl32 = (depth > 16);
    m_body << "-- SRL" << (srl32 ? 32 : 16) << " FIFO\n";
    m_body << name << " : entity work."
           << (srl32 ? VHDLHelpers::FIFO_SRL32_COMPONENT_NAME : VHDLHelpers::FIFO_SRL16_COMPONENT_NAME) << "(behav)\n";
    m_body << "  generic map (\n";
    m_body << "    DATA_WIDTH => " << data_width << ",\n";
    m_body << "  )\n";
    m_body << "  port map (\n";
    m_body << "    clk => clk,\n";
    m_body << "    rst_n => rst_n,\n";
    m_body << "    read => " << name << "_read,\n";
    m_body << "    write => " << name << "_write,\n";
    m_body << "    empty_n => " << name << "_empty_n,\n";
    m_body << "    full_n => " << name << "_full_n,\n";
    m_body << "    dout => " << name << "_dout,\n";
    m_body << "    din => " << name << "_din\n";
    m_body << "  );\n";
    m_body << "\n";
  }
  else
  {
    m_body << "-- FIFO with depth " << depth << "\n";
    // m_body << name << " : " << FIFO_COMPONENT_NAME << "\n";
    m_body << name << " : entity work." << VHDLHelpers::FIFO_COMPONENT_NAME << "(behav)\n";
    m_body << "  generic map (\n";
    m_body << "    DATA_WIDTH => " << data_width << ",\n";
    m_body << "    SIZE => " << depth << "\n";
    m_body << "  )\n";
    m_body << "  port map (\n";
    m_body << "    clk => clk,\n";
    m_body << "    rst_n => rst_n,\n";
    m_body << "    read => " << name << "_read,\n";
    m_body << "    write => " << name << "_write,\n";
    m_body << "    empty_n => " << name << "_empty_n,\n";
    m_body << "    full_n => " << name << "_full_n,\n";
    m_body << "    dout => " << name << "_dout,\n";
    m_body << "    din => " << name << "_din\n";
    m_body << "  );\n";
    m_body << "\n";
  }
}

void ComponentGenerator::WriteFilterInstance(StreamGraph::Filter* node)
{
  const std::string& name = node->GetName();

  // Input FIFO queue
  if (!node->GetInputType()->isVoidTy())
  {
    u32 fifo_depth = std::max(node->GetNetPeek(), node->GetNetPop()) * VHDLHelpers::FIFO_SIZE_MULTIPLIER;
    WriteFIFO(StringFromFormat("%s_fifo", name.c_str()), VHDLHelpers::GetBitWidthForType(node->GetInputType()),
              fifo_depth);
  }

  // m_body << name << " : filter_" << node->GetFilterPermutation()->GetName() << "\n";
  m_body << name << " : entity work.filter_" << node->GetFilterPermutation()->GetName() << "(behav)\n";
  m_body << "  port map (\n";
  m_body << "    ap_clk => clk,\n";
  m_body << "    ap_rst_n => rst_n";
  if (!node->GetInputType()->isVoidTy())
  {
    m_body << ",\n";
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_dout => " << name << "_fifo_dout,\n";
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_read => " << name << "_fifo_read,\n";
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_ptr_empty_n => " << name << "_fifo_empty_n";
  }
  if (!node->GetOutputType()->isVoidTy())
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
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_din => prog_output_din,\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_write => prog_output_write,\n";
      m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "out_ptr_full_n => prog_output_full_n";
    }
  }
  m_body << "\n";
  m_body << "  );\n";
  m_body << "\n";
}

void ComponentGenerator::WriteCombinationalFilterInstance(StreamGraph::Filter* node)
{
  const std::string& name = node->GetName();

  // Combinational filter
  if (!node->GetInputType()->isVoidTy())
  {
    m_signals << "signal " << name << "_fifo_write : std_logic;\n";
    m_signals << "signal " << name << "_fifo_full_n : std_logic;\n";
    m_signals << "signal " << name << "_fifo_din : std_logic_vector("
              << (VHDLHelpers::GetBitWidthForType(node->GetInputType()) - 1) << " downto 0);\n";
  }

  m_body << name << " : entity work.filter_" << node->GetFilterPermutation()->GetName() << "(behav)\n";
  m_body << "  port map (\n";
  if (!node->GetInputType()->isVoidTy())
  {
    m_body << "    " << VHDLHelpers::HLS_VARIABLE_PREFIX << "in_value => " << name << "_fifo_din";
    if (!node->GetOutputType()->isVoidTy())
      m_body << ",\n";
    else
      m_body << "\n";
  }
  if (!node->GetOutputType()->isVoidTy())
  {
    if (!node->GetOutputChannelName().empty())
      m_body << "    ap_return => " << node->GetOutputChannelName() << "_fifo_din\n";
    else
      m_body << "    ap_return => prog_output_din\n";
  }
  m_body << "  );\n";

  // Tie write/full signals to the next filter in the chain
  if (!node->GetInputType()->isVoidTy() && !node->GetOutputType()->isVoidTy())
  {
    const std::string& output_name = node->GetOutputChannelName();
    if (!output_name.empty())
    {
      m_body << name << "_write <= " << output_name << "_fifo_write;\n";
      m_body << name << "_full_n <= " << output_name << "_fifo_full_n;\n";
    }
    else
    {
      m_body << name << "_write <= " << output_name << "prog_output_write;\n";
      m_body << name << "_full_n <= " << output_name << "prog_output_full_n;\n";
    }
  }
  else if (!node->GetInputType()->isVoidTy())
  {
    // If there is an input type, tie it to always push.
    m_body << name << "_full_n <= '0';\n";
  }
  m_body << "\n";
}

bool ComponentGenerator::Visit(StreamGraph::Filter* node)
{
  const std::string& name = node->GetName();
  m_body << "-- Filter instance " << name << " (filter " << node->GetFilterPermutation()->GetName() << ")\n";

  // Component instantiation
  if (node->GetFilterPermutation()->IsCombinational())
    WriteCombinationalFilterInstance(node);
  else
    WriteFilterInstance(node);

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
  if (!node->GetSplitNode()->Accept(this))
    return false;

  for (StreamGraph::Node* child : node->GetChildren())
  {
    if (!child->Accept(this))
      return false;
  }

  if (!node->GetJoinNode()->Accept(this))
    return false;

  return true;
}

void ComponentGenerator::WriteSplitDuplicate(const StreamGraph::Split* node)
{
  const std::string& name = node->GetName();
  std::string fifo_name = StringFromFormat("%s_fifo", name.c_str());
  u32 data_width = VHDLHelpers::GetBitWidthForType(node->GetInputType());
  u32 fifo_depth = node->GetNetPop() * VHDLHelpers::FIFO_SIZE_MULTIPLIER;
  WriteFIFO(fifo_name, data_width, fifo_depth);

  // Split process
  m_body << "-- split duplicate node " << name << " with " << node->GetOutputChannelNames().size()
         << " outgoing streams\n";
  m_signals << "signal " << name << "_write : std_logic;\n";

  // Signals are wired directly, this saves creating a register between the incoming and outgoing filters.
  // TODO: Remove the FIFO queue for the split completely.
  m_body << name << "_write <= (" << fifo_name << "_empty_n";
  for (const std::string& output_name : node->GetOutputChannelNames())
    m_body << " and " << output_name << "_fifo_full_n";
  m_body << ");\n";
  m_body << fifo_name << "_read <= " << name << "_write;\n";
  for (const std::string& output_name : node->GetOutputChannelNames())
  {
    m_body << output_name << "_fifo_din <= " << fifo_name << "_dout;\n";
    m_body << output_name << "_fifo_write <= " << name << "_write;\n";
  }

  m_body << "\n";
}

void ComponentGenerator::WriteSplitRoundrobin(const StreamGraph::Split* node)
{
  const std::string& name = node->GetName();
  std::string fifo_name = StringFromFormat("%s_fifo", name.c_str());
  u32 data_width = VHDLHelpers::GetBitWidthForType(node->GetInputType());
  u32 fifo_depth = node->GetNetPop() * VHDLHelpers::FIFO_SIZE_MULTIPLIER;
  WriteFIFO(fifo_name, data_width, fifo_depth);

  // Splitjoin state type
  std::string state_signal = StringFromFormat("%s_state", name.c_str());
  m_signals << "type " << name << "_state_type is (";
  for (u32 idx = 1; idx <= node->GetNumOutputChannels(); idx++)
  {
    for (u32 step = 1; step <= node->GetDistribution().at(idx - 1); step++)
      m_signals << ((idx == 1 && step == 1) ? "" : ", ") << name << "_state_" << idx << "_step_" << step;
  }

  m_signals << ");\n";
  m_signals << "signal " << state_signal << " : " << name << "_state_type;\n";

  // Splitjoin process
  m_body << "-- split roundrobin node " << name << " with " << node->GetNumOutputChannels() << " outgoing streams\n";

  // All the din ports can be mapped to the fifo output, we adjust the write port instead.
  for (const std::string& output_name : node->GetOutputChannelNames())
    m_body << output_name << "_din <= " << fifo_name << "_dout;\n";

  m_body << name << "_split_process : process(clk)\n";
  m_body << "begin\n";
  m_body << "  if (rising_edge(clk)) then\n";
  m_body << "    if (rst_n = '0') then\n";
  m_body << "      " << state_signal << " <= " << name << "_state_step_1;\n";
  m_body << "    else\n";
  m_body << "      case " << state_signal << " is\n";
  for (u32 idx = 1; idx <= node->GetNumOutputChannels(); idx++)
  {
    for (u32 step = 1; step <= node->GetDistribution().at(idx - 1); step++)
    {
      const std::string& output_name = node->GetOutputChannelNames().at(idx - 1);
      bool last_step = (step == node->GetDistribution().at(idx - 1));
      u32 next_state = last_step ? ((idx % node->GetNumOutputChannels()) + 1) : idx;
      u32 next_step = last_step ? 1 : (step + 1);
      m_body << "        when " << name << "_state_" << idx << "_step_" << step << " =>\n";
      m_body << "          if (" << fifo_name << "_empty_n = '1' and " << output_name << "_full_n = '1') then\n";
      m_body << "            " << fifo_name << "_read <= '1';\n";
      m_body << "            " << output_name << "_write <= '1';\n";
      m_body << "            " << state_signal << " <= " << name << "_state_" << next_state << ";\n";
      m_body << "          else\n";
      m_body << "            " << fifo_name << "_read <= '0';\n";
      m_body << "            " << output_name << "_write <= '0';\n";
      m_body << "          end if;\n";
      for (u32 other_idx = 1; other_idx <= node->GetNumOutputChannels(); other_idx++)
      {
        if (other_idx == idx)
          continue;
        m_body << "          " << node->GetOutputChannelNames().at(other_idx - 1) << "_fifo_read <= '0';\n";
      }
    }
  }
  m_body << "      end case;\n";
  m_body << "    end if;\n";
  m_body << "  end if;\n";
  m_body << "end process;\n";
  m_body << "\n";
}

bool ComponentGenerator::Visit(StreamGraph::Split* node)
{
  if (node->GetMode() == StreamGraph::Split::Mode::Duplicate)
    WriteSplitDuplicate(node);
  else
    WriteSplitRoundrobin(node);

  return true;
}

bool ComponentGenerator::Visit(StreamGraph::Join* node)
{
  const std::string& name = node->GetName();
  std::string output_name;
  if (node->GetOutputChannelName().empty())
    output_name = "prog_output";
  else
    node->GetOutputChannelName() + "_fifo";

  // Generate fifos for each input to the join
  for (u32 idx = 1; idx <= node->GetIncomingStreams(); idx++)
  {
    std::string fifo_name = StringFromFormat("%s_%u_fifo", name.c_str(), idx);
    u32 data_width = VHDLHelpers::GetBitWidthForType(node->GetInputType());
    u32 fifo_depth = node->GetNetPop() * VHDLHelpers::FIFO_SIZE_MULTIPLIER;
    WriteFIFO(fifo_name, data_width, fifo_depth);
  }

  // Splitjoin state type
  std::string state_signal = StringFromFormat("%s_state", name.c_str());
  m_signals << "type " << name << "_state_type is (";
  for (u32 idx = 1; idx <= node->GetIncomingStreams(); idx++)
  {
    for (u32 step = 1; step <= node->GetDistribution().at(idx - 1); step++)
      m_signals << ((idx == 1 && step == 1) ? "" : ", ") << name << "_state_" << idx << "_step_" << step;
  }
  m_signals << ");\n";
  m_signals << "signal " << state_signal << " : " << name << "_state_type;\n";

  // Splitjoin process
  m_body << "-- join node " << name << " with " << node->GetIncomingStreams() << " incoming streams\n";
  m_body << name << "_join_process : process(clk)\n";
  m_body << "begin\n";
  m_body << "  if (rising_edge(clk)) then\n";
  m_body << "    if (rst_n = '0') then\n";
  m_body << "      " << state_signal << " <= " << name << "_state_1_step_1;\n";
  m_body << "    else\n";
  m_body << "      case " << state_signal << " is\n";
  for (u32 idx = 1; idx <= node->GetIncomingStreams(); idx++)
  {
    std::string fifo_name = StringFromFormat("%s_%u_fifo", name.c_str(), idx);
    for (u32 step = 1; step <= node->GetDistribution().at(idx - 1); step++)
    {
      bool last_step = (step == node->GetDistribution().at(idx - 1));
      u32 next_state = last_step ? ((idx % node->GetIncomingStreams()) + 1) : idx;
      u32 next_step = last_step ? 1 : (step + 1);
      m_body << "        when " << name << "_state_" << idx << "_step_" << step << " =>\n";
      m_body << "          " << output_name << "_din <= " << fifo_name << "_dout;\n";
      m_body << "          if (" << fifo_name << "_empty_n = '1' and " << output_name << "_full_n = '1') then\n";
      m_body << "            " << fifo_name << "_read <= '1';\n";
      m_body << "            " << output_name << "_write <= '1';\n";
      m_body << "            " << state_signal << " <= " << name << "_state_" << next_state << "_step_" << next_step
             << ";\n";
      m_body << "          else\n";
      m_body << "            " << fifo_name << "_read <= '0';\n";
      m_body << "            " << output_name << "_write <= '0';\n";
      m_body << "          end if;\n";
      for (u32 other_idx = 1; other_idx <= node->GetIncomingStreams(); other_idx++)
      {
        if (other_idx == idx)
          continue;
        m_body << "          " << name << "_" << other_idx << "_fifo_read <= '0';\n";
      }
    }
  }
  m_body << "      end case;\n";
  m_body << "    end if;\n";
  m_body << "  end if;\n";
  m_body << "end process;\n";
  m_body << "\n";

  return true;
}

} // namespace HLSTarget
