#include "hlstarget/component_test_bench_generator.h"
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
Log_SetChannel(HLSTarget::ComponentTestBenchGenerator);

namespace HLSTarget
{
ComponentTestBenchGenerator::ComponentTestBenchGenerator(WrappedLLVMContext* context,
                                                         StreamGraph::StreamGraph* streamgraph,
                                                         const std::string& module_name, llvm::raw_fd_ostream& os)
  : m_context(context), m_streamgraph(streamgraph), m_module_name(module_name), m_os(os)
{
}

ComponentTestBenchGenerator::~ComponentTestBenchGenerator()
{
}

bool ComponentTestBenchGenerator::GenerateTestBench()
{
  WriteHeader();
  WriteClockGenerator();
  WriteInputGenerator();
  WriteOutputConsumer();
  WriteWrapperComponent();
  WriteResetProcess();

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

void ComponentTestBenchGenerator::WriteHeader()
{
  m_os << "library IEEE;\n";
  m_os << "use IEEE.STD_LOGIC_1164.ALL;\n";
  m_os << "use IEEE.NUMERIC_STD.ALL;\n";
  m_os << "\n";

  m_os << "entity " << m_module_name << "_tb is\n";
  m_os << "end " << m_module_name << "_tb;\n";
  m_os << "\n";

  m_os << "architecture behav of " << m_module_name << "_tb is\n";

  m_signals << "constant CLK_PERIOD : time := 1ns;\n";
  m_signals << "signal clk : std_logic := '0';\n";
  m_signals << "signal runsim : std_logic := '1';\n";
  m_signals << "signal rst_n : std_logic := '0';\n";
  m_signals << "\n";
}

void ComponentTestBenchGenerator::WriteFooter()
{
  m_os << "end behav;\n";
}

void ComponentTestBenchGenerator::WriteWrapperComponent()
{
  // Component instantiation
  m_body << m_module_name << "_comp : entity work." << m_module_name << "(behav)\n";
  m_body << "  port map (\n";
  m_body << "    clk => clk,\n";
  m_body << "    rst_n => rst_n";
  if (!m_streamgraph->GetProgramInputType()->IsVoid())
  {
    m_body << ",\n";
    m_body << "    prog_input_dout => input_fifo_dout,\n";
    m_body << "    prog_input_read => input_fifo_read,\n";
    m_body << "    prog_input_empty_n => input_fifo_empty_n";
  }
  if (!m_streamgraph->GetProgramOutputType()->IsVoid())
  {
    m_body << ",\n";
    m_body << "    prog_output_din => output_fifo_din,\n";
    m_body << "    prog_output_write => output_fifo_write,\n";
    m_body << "    prog_output_full_n => output_fifo_full_n";
  }
  m_body << "\n";
  m_body << "  );\n";
  m_body << "\n";
}

void ComponentTestBenchGenerator::WriteInputGenerator()
{
  const Type* program_input_type = m_streamgraph->GetProgramInputType();
  if (program_input_type->IsVoid())
    return;

  m_signals << "-- Input FIFO queue\n";
  m_signals << "signal input_fifo_read : std_logic;\n";
  m_signals << "signal input_fifo_write : std_logic;\n";
  m_signals << "signal input_fifo_empty_n : std_logic;\n";
  m_signals << "signal input_fifo_full_n : std_logic;\n";
  m_signals << "signal input_fifo_dout : " << VHDLHelpers::GetVHDLBitVectorType(program_input_type) << ";\n";
  m_signals << "signal input_fifo_din : " << VHDLHelpers::GetVHDLBitVectorType(program_input_type) << ";\n";
  m_signals << "\n";

  m_body << "-- Input FIFO queue\n";
  m_body << "input_fifo : entity work." << VHDLHelpers::FIFO_COMPONENT_NAME << "(behav)\n";
  m_body << "  generic map (\n";
  m_body << "    DATA_WIDTH => " << VHDLHelpers::GetBitWidthForType(program_input_type) << ",\n";
  m_body << "    SIZE => 16\n";
  m_body << "  )\n";
  m_body << "  port map (\n";
  m_body << "    clk => clk,\n";
  m_body << "    rst_n => rst_n,\n";
  m_body << "    read => input_fifo_read,\n";
  m_body << "    write => input_fifo_write,\n";
  m_body << "    empty_n => input_fifo_empty_n,\n";
  m_body << "    full_n => input_fifo_full_n,\n";
  m_body << "    dout => input_fifo_dout,\n";
  m_body << "    din => input_fifo_din\n";
  m_body << "  );\n";
  m_body << "\n";

  // TODO: data generation
}

void ComponentTestBenchGenerator::WriteOutputConsumer()
{
  const Type* program_output_type = m_streamgraph->GetProgramOutputType();
  if (program_output_type->IsVoid())
    return;

  // output FIFO queue
  m_signals << "-- Output FIFO queue\n";
  m_signals << "signal output_fifo_read : std_logic;\n";
  m_signals << "signal output_fifo_write : std_logic;\n";
  m_signals << "signal output_fifo_empty_n : std_logic;\n";
  m_signals << "signal output_fifo_full_n : std_logic;\n";
  m_signals << "signal output_fifo_dout : " << VHDLHelpers::GetVHDLBitVectorType(program_output_type) << ";\n";
  m_signals << "signal output_fifo_din : " << VHDLHelpers::GetVHDLBitVectorType(program_output_type) << ";\n";
  m_signals << "\n";

  m_body << "-- Output FIFO queue\n";
  m_body << "output_fifo : entity work." << VHDLHelpers::FIFO_COMPONENT_NAME << "(behav)\n";
  m_body << "  generic map (\n";
  m_body << "    DATA_WIDTH => " << VHDLHelpers::GetBitWidthForType(program_output_type) << ",\n";
  m_body << "    SIZE => 16\n";
  m_body << "  )\n";
  m_body << "  port map (\n";
  m_body << "    clk => clk,\n";
  m_body << "    rst_n => rst_n,\n";
  m_body << "    read => output_fifo_read,\n";
  m_body << "    write => output_fifo_write,\n";
  m_body << "    empty_n => output_fifo_empty_n,\n";
  m_body << "    full_n => output_fifo_full_n,\n";
  m_body << "    dout => output_fifo_dout,\n";
  m_body << "    din => output_fifo_din\n";
  m_body << "  );\n";
  m_body << "\n";

  m_body << "-- Output FIFO queue consume process\n";
  m_body << "output_fifo_consume : process(clk)\n";
  m_body << "begin\n";
  m_body << "  if (rising_edge(clk)) then\n";
  m_body << "    if (output_fifo_empty_n = '1') then\n";
  m_body << "      output_fifo_read <= '1';\n";
  m_body << "      report \"Program output \" & integer'image(to_integer(signed(output_fifo_dout)));\n";
  m_body << "    else\n";
  m_body << "      output_fifo_read <= '0';\n";
  m_body << "    end if;\n";
  m_body << "  end if;\n";
  m_body << "end process;\n";
  m_body << "\n";
}

void ComponentTestBenchGenerator::WriteClockGenerator()
{
  m_body << "-- Test bench clock generator\n";
  m_body << "clock_generator : process\n";
  m_body << "begin\n";
  m_body << "  if (runsim = '1') then\n";
  m_body << "    wait for CLK_PERIOD / 2;\n";
  m_body << "    clk <= not clk;\n";
  m_body << "  else\n";
  m_body << "    wait;\n";
  m_body << "  end if;\n";
  m_body << "end process;\n";
  m_body << "\n";
}

void ComponentTestBenchGenerator::WriteResetProcess()
{
  m_body << "-- Reset process\n";
  m_body << "reset_process : process\n";
  m_body << "begin\n";
  m_body << "  rst_n <= '0';\n";
  m_body << "  wait for 1ns;\n";
  m_body << "\n";
  m_body << "  rst_n <= '1';\n";
  m_body << "  wait for 500ns;\n";
  m_body << "\n";
  m_body << "  runsim <= '0';\n";
  m_body << "  wait;\n";
  m_body << "end process;\n";
  m_body << "\n";
}

} // namespace HLSTarget
