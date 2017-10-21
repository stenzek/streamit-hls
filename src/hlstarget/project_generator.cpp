#include "hlstarget/project_generator.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "frontend/wrapped_llvm_context.h"
#include "hlstarget/component_generator.h"
#include "hlstarget/component_test_bench_generator.h"
#include "hlstarget/filter_builder.h"
#include "hlstarget/test_bench_generator.h"
#include "hlstarget/vhdl_helpers.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(HLSTarget::ProjectGenerator);

extern void addCBackendPasses(llvm::legacy::PassManagerBase& PM, llvm::raw_pwrite_stream& Out);

namespace HLSTarget
{
ProjectGenerator::ProjectGenerator(Frontend::WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
                                   const std::string& module_name, const std::string& output_dir)
  : m_context(context), m_streamgraph(streamgraph), m_module_name(module_name), m_output_dir(output_dir)
{
}

ProjectGenerator::~ProjectGenerator()
{
  delete m_module;
}

static bool debug_opt = false;

bool ProjectGenerator::GenerateCode()
{
  CreateModule();

  if (!GenerateFilterFunctions())
    return false;

  if (debug_opt)
  {
    Log_InfoPrintf("IR prior to optimization");
    m_context->DumpModule(m_module);
  }

  OptimizeModule();

  if (debug_opt)
  {
    Log_InfoPrintf("IR after optimization");
    m_context->DumpModule(m_module);
  }

  return true;
}

bool ProjectGenerator::GenerateProject()
{
  if (!CleanOutputDirectory())
  {
    Log_ErrorPrintf("Failed to clean output directory.");
    return false;
  }

  if (!WriteCCode())
  {
    Log_ErrorPrintf("Failed to write C code.");
    return false;
  }

  if (!GenerateCTestBench())
  {
    Log_ErrorPrintf("Failed to generate C test bench.");
    return false;
  }

  if (!WriteHLSScript())
  {
    Log_ErrorPrintf("Failed to write HLS script.");
    return false;
  }

  if (!WriteFIFOComponent())
  {
    Log_ErrorPrintf("Failed to write FIFO component.");
    return false;
  }

  if (!GenerateComponent())
  {
    Log_ErrorPrintf("Failed to generate component.");
    return false;
  }

  if (!GenerateAXISComponent())
  {
    Log_ErrorPrintf("Failed to generate AXIS component.");
    return false;
  }

  if (!GenerateComponentTestBench())
  {
    Log_ErrorPrintf("Failed to generate component test bench.\n");
    return false;
  }

  if (!WriteVivadoScript())
  {
    Log_ErrorPrintf("Failed to write Vivado script.");
    return false;
  }

  return true;
}

void ProjectGenerator::CreateModule()
{
  m_module = m_context->CreateModule(m_module_name.c_str());
  Log_InfoPrintf("Module name is '%s'", m_module_name.c_str());
}

bool ProjectGenerator::GenerateFilterFunctions()
{
  Log_InfoPrintf("Generating filter and channel functions...");

  for (const StreamGraph::FilterPermutation* filter_perm : m_streamgraph->GetFilterPermutationList())
  {
    Log_InfoPrintf("Generating filter function for %s", filter_perm->GetName().c_str());

    FilterBuilder fb(m_context, m_module, filter_perm);
    if (!fb.GenerateCode())
      return false;

    // Can we turn this into a combinational filter?
    llvm::Function* filter_func = fb.GetFunction();
    if (fb.CanMakeCombinational())
    {
      Log_WarningPrintf("Making filter %s combinational", filter_perm->GetName().c_str());
      const_cast<StreamGraph::FilterPermutation*>(filter_perm)->SetCombinational();
    }

    auto res = m_filter_function_map.emplace(filter_perm, filter_func);
    assert(res.second);
  }

  return true;
}

void ProjectGenerator::OptimizeModule()
{
  Log_InfoPrintf("Optimizing LLVM IR...");

  llvm::legacy::FunctionPassManager fpm(m_module);
  llvm::legacy::PassManager mpm;

  // Use standard -O2 optimizations, except disable loop unrolling.
  llvm::PassManagerBuilder builder;
  builder.OptLevel = 2;
  builder.DisableTailCalls = true;
  builder.DisableUnrollLoops = true;

  // Add loop unrolling passes afterwards with more aggressive parameters.
  builder.addExtension(llvm::PassManagerBuilder::EP_LoopOptimizerEnd, [](const llvm::PassManagerBuilder& builder,
                                                                         llvm::legacy::PassManagerBase& pm) {
    pm.add(llvm::createLoopUnrollPass(/*std::numeric_limits<int>::max()*/ -1 /* threshold */, -1 /* count */,
                                      -1 /* allowpartial */, -1 /* runtime */, 0 /* upperbound */));
  });

  builder.populateFunctionPassManager(fpm);
  builder.populateModulePassManager(mpm);

  fpm.doInitialization();
  for (llvm::Function& F : *m_module)
    fpm.run(F);
  fpm.doFinalization();

  mpm.run(*m_module);
}

bool ProjectGenerator::CleanOutputDirectory()
{
  //   // TODO: Re-add after moving to LLVM 5.0.
  //   Log_InfoPrintf("Cleaning output directory '%s'...", m_output_dir.c_str());
  //   auto ec = llvm::sys::fs::remove_directories(m_output_dir);
  //   if (ec)
  //   {
  //     Log_ErrorPrintf("Failed to remove output directory '%s'", m_output_dir.c_str());
  //     return false;
  //   }

  std::error_code ec;
  if ((ec = llvm::sys::fs::create_directory(m_output_dir)) ||
      (ec = llvm::sys::fs::create_directory(StringFromFormat("%s/_autogen_hls", m_output_dir.c_str()))) ||
      (ec = llvm::sys::fs::create_directory(StringFromFormat("%s/_autogen_vhdl", m_output_dir.c_str()))))
  {
    Log_ErrorPrintf("Failed to create output directory '%s'", m_output_dir.c_str());
    return false;
  }

  return true;
}

bool ProjectGenerator::WriteCCode()
{
  std::string c_code_filename = StringFromFormat("%s/_autogen_hls/filters.c", m_output_dir.c_str());
  Log_InfoPrintf("Writing C code to %s...", c_code_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(c_code_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  llvm::legacy::PassManager pm;
  addCBackendPasses(pm, os);
  pm.run(*m_module);
  return true;
}

bool ProjectGenerator::GenerateCTestBench()
{
  Log_InfoPrintf("Generating test benches...");

  std::string testbench_module_name = StringFromFormat("%s_test_benches", m_module_name.c_str());
  TestBenchGenerator generator(m_context, m_streamgraph, testbench_module_name, m_output_dir);
  return generator.GenerateTestBenches();
}

bool ProjectGenerator::GenerateComponent()
{
  std::string filename = StringFromFormat("%s/_autogen_vhdl/%s.vhd", m_output_dir.c_str(), m_module_name.c_str());
  Log_InfoPrintf("Writing wrapper component to %s...", filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  ComponentGenerator cg(m_context, m_streamgraph, m_module_name, os);
  if (!cg.GenerateComponent())
    return false;

  os.flush();
  return true;
}

bool ProjectGenerator::GenerateComponentTestBench()
{
  std::string filename = StringFromFormat("%s/_autogen_vhdl/%s_tb.vhd", m_output_dir.c_str(), m_module_name.c_str());
  Log_InfoPrintf("Writing wrapper test bench to %s...", filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  ComponentTestBenchGenerator cg(m_context, m_streamgraph, m_module_name, os);
  if (!cg.GenerateTestBench())
    return false;

  os.flush();
  return true;
}

bool ProjectGenerator::GenerateAXISComponent()
{
  if (!m_streamgraph->HasProgramInputNode() || !m_streamgraph->HasProgramOutputNode())
  {
    Log_WarningPrintf("Not generating AXIS component, missing input or output types.");
    m_has_axis_component = false;
    return true;
  }

  std::string filename = StringFromFormat("%s/_autogen_vhdl/axis_%s.vhd", m_output_dir.c_str(), m_module_name.c_str());
  Log_InfoPrintf("Writing AXIS component to %s...", filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  u32 in_width =
    VHDLHelpers::GetBitWidthForType(m_streamgraph->GetProgramInputType()) * m_streamgraph->GetProgramInputWidth();
  u32 out_width =
    VHDLHelpers::GetBitWidthForType(m_streamgraph->GetProgramOutputType()) * m_streamgraph->GetProgramOutputWidth();

  // Seems we can use a maximum of around 2048 doublewords/8192 bytes before the DMA engine gets stuck..
  u32 out_block_size = 8192 / ((out_width + 7) / 8);

  os << "library ieee;\n"
     << "use ieee.std_logic_1164.all;\n"
     << "use ieee.std_logic_unsigned.all;\n"
     << "use ieee.numeric_std.all;\n"
     << "\n";

  os << "entity axis_" << m_module_name << " is\n";
  os << "  port (\n";
  os << "    aclk : in std_logic;\n";
  os << "    aresetn : in std_logic;\n";
  os << "    -- axi4 stream slave (data input)\n";
  os << "    s_axis_tdata : in std_logic_vector(" << in_width << "-1 downto 0);\n";
  os << "    s_axis_tvalid : in std_logic;\n";
  os << "    s_axis_tready : out std_logic;\n";
  os << "    s_axis_tlast : in std_logic;\n";
  os << "    -- axi4 stream master (data output)\n";
  os << "    m_axis_tdata : out std_logic_vector(" << out_width << "-1 downto 0);\n";
  os << "    m_axis_tvalid : out std_logic;\n";
  os << "    m_axis_tready : in std_logic;\n";
  os << "    m_axis_tlast : out std_logic\n";
  os << "  );\n";
  os << "end entity;\n";
  os << "\n";

  os << "architecture behav of axis_" << m_module_name << " is\n";
  os << "  constant OUTPUT_BLOCK_SIZE : positive := " << out_block_size << ";\n";
  os << "  signal prog_output_din : std_logic_vector(" << out_width << "-1 downto 0);\n";
  os << "  signal prog_output_full_n : std_logic;\n";
  os << "  signal prog_output_write : std_logic;\n";
  os << "  signal prog_output_last : std_logic;\n";
  os << "  signal output_counter : positive range 0 to OUTPUT_BLOCK_SIZE;\n";
  os << "  signal output_last : boolean;\n";
  os << "begin\n";
  os << "\n";
  os << "  -- Instantiate wrapper component\n";
  os << "  " << m_module_name << "_comp : entity work." << m_module_name << "(behav)\n";
  os << "    port map (\n";
  os << "      clk => aclk,\n";
  os << "      rst_n => aresetn,\n";
  os << "      prog_input_din => s_axis_tdata,\n";
  os << "      prog_input_write => s_axis_tvalid,\n";
  os << "      prog_input_full_n => s_axis_tready,\n";
  os << "      prog_output_din => prog_output_din,\n";
  os << "      prog_output_full_n => prog_output_full_n,\n";
  os << "      prog_output_write => prog_output_write\n";
  os << "    );\n";
  os << "\n";
  os << "  -- Wires to AXIS master\n";
  os << "  m_axis_tdata <= prog_output_din;\n";
  os << "  prog_output_full_n <= m_axis_tready;\n";
  os << "  m_axis_tvalid <= prog_output_write;\n";
  os << "  m_axis_tlast <= prog_output_last;\n";
  os << "\n";

  os << "  -- TLAST tracking\n";
  os << "  output_last <= (prog_output_write = '1' and (output_counter + 1) = OUTPUT_BLOCK_SIZE);\n";
  os << "  prog_output_last <= '1' when output_last else '0';\n";
  os << "\n";
  os << "  last_process : process(aclk)\n";
  os << "  begin\n";
  os << "    if (rising_edge(aclk)) then\n";
  os << "      if (aresetn = '0') then\n";
  os << "        output_counter <= 0;\n";
  os << "      else\n";
  os << "        if (prog_output_write = '1') then\n";
  os << "          if ((output_counter + 1) = OUTPUT_BLOCK_SIZE) then\n";
  os << "            output_counter <= 0;\n";
  os << "          else\n";
  os << "            output_counter <= output_counter + 1;\n";
  os << "          end if;\n";
  os << "        end if;\n";
  os << "      end if;\n";
  os << "    end if;\n";
  os << "  end process;\n";
  os << "\n";

  os << "end behav;\n";

  os.flush();
  m_has_axis_component = true;
  return true;
}

bool ProjectGenerator::WriteHLSScript()
{
  std::string script_filename = StringFromFormat("%s/_autogen_hls/script.tcl", m_output_dir.c_str());
  Log_InfoPrintf("Writing HLS TCL script to %s...", script_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(script_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  os << "open_project -reset " << m_module_name << "\n";
  os << "add_files filters.c\n";
  os << "add_files -tb filters_tb.c\n";
  os << "\n";

  for (const auto& it : m_filter_function_map)
  {
    const StreamGraph::FilterPermutation* filter_perm = it.first;
    if (!it.second)
    {
      // Some filters are only used to imply statements in the VHDL component.
      // These include InputReader/OutputWriter. Skip synthesis in this case.
      continue;
    }

    const std::string& filter_name = filter_perm->GetName();
    const std::string function_name = StringFromFormat("filter_%s", filter_name.c_str());
    os << "# filter " << filter_name << "\n"
       << "open_solution -reset \"" << function_name << "\"\n"
       << "set_top " << function_name << "\n"
       << "set_part {" << VHDLHelpers::TARGET_PART_ID << "} -tool vivado\n"
       << "create_clock -period 10 -name default\n"
       << "\n";

    os << "# directives\n";

    // The default max name length is 20, which results in conflicts when creating
    // multiple multiply cores for example across multiple filters. 80 should be more
    // than sufficient, if not overkill.
    os << "config_compile -name_max_length 80\n";

    // Since all our static variables are "state", they must be initialized at reset.
    // We don't use statics for any data storage, except for the FIFO peek buffer.
    // TODO: We can optimize this by setting them specifically instead of globally,
    // set_directive_reset "filter_counter" counter_last
    os << "config_rtl -reset state -reset_level low\n";

    // Disable handshake signals on block, we don't need them, since use the fifo for control
    os << "set_directive_interface -mode ap_ctrl_none \"" << function_name << "\"\n";

    // For non-combinational filters use ap_none else FIFO
    const char* param_interface = filter_perm->IsCombinational() ? "ap_none" : "ap_fifo";

    // Make input pointer a fifo
    if (!filter_perm->GetInputType()->isVoidTy())
    {
      u32 depth = std::max(filter_perm->GetPeekRate() / filter_perm->GetInputChannelWidth(),
                           filter_perm->GetPopRate() / filter_perm->GetInputChannelWidth());
      for (u32 i = 0; i < filter_perm->GetInputChannelWidth(); i++)
      {
        os << "set_directive_interface -mode " << param_interface << " -depth " << depth << " \"" << function_name
           << "\" in_channel_" << i << "\n";
      }
    }

    // Make output pointer a fifo
    if (!filter_perm->GetOutputType()->isVoidTy())
    {
      for (u32 i = 0; i < filter_perm->GetOutputChannelWidth(); i++)
      {
        os << "set_directive_interface -mode " << param_interface << " -depth "
           << (filter_perm->GetPushRate() / filter_perm->GetOutputChannelWidth()) << " \"" << function_name
           << "\" out_channel_" << i << "\n";
      }
    }

    os << "\n";

    os << "# commands\n";
    os << "csynth_design\n";
    // os << "csim_design -argv \"" << function_name << "\"\n";
    os << "\n";
  }

  os << "exit\n";
  os.flush();
  return !os.has_error();
}

bool ProjectGenerator::WriteVivadoScript()
{
  std::string script_filename = StringFromFormat("%s/create_project.tcl", m_output_dir.c_str());
  Log_InfoPrintf("Writing Vivado TCL script to %s...", script_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(script_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  os << "create_project " << m_module_name << " . -part " << VHDLHelpers::TARGET_PART_ID << " -force\n";
  os << "set_property target_language VHDL [current_project]\n";
  os << "\n";

  // Add HLS output files.
  // Generate IP cores for floating-point units.
  // There can be more than one VHDL file per HLS solution.
  os << "# Add HLS outputs\n";
  for (const auto& it : m_filter_function_map)
  {
    if (!it.second)
      continue;

    const StreamGraph::FilterPermutation* filter_perm = it.first;
    const std::string& filter_name = filter_perm->GetName();
    const std::string function_name = StringFromFormat("filter_%s", filter_name.c_str());
    os << "add_files -norecurse [glob ./_autogen_hls/" << m_module_name << "/" << function_name << "/syn/vhdl/*.vhd]\n";
    os << "foreach tclfile [glob -nocomplain ./_autogen_hls/" << m_module_name << "/" << function_name
       << "/syn/vhdl/*.tcl] { source $tclfile }\n";
  }
  os << "\n";

  // Add wrapper component.
  os << "add_files \"./_autogen_vhdl/fifo.vhd\"\n";
  os << "add_files \"./_autogen_vhdl/fifo_srl16.vhd\"\n";
  os << "add_files \"./_autogen_vhdl/fifo_srl32.vhd\"\n";
  os << "add_files \"./_autogen_vhdl/" << m_module_name << ".vhd\"\n";
  os << "add_files -fileset sim_1 \"./_autogen_vhdl/" << m_module_name << "_tb.vhd\"\n";
  if (m_has_axis_component)
    os << "add_files \"./_autogen_vhdl/axis_" << m_module_name << ".vhd\"\n";
  os << "\n";

  // Import into main project.
  os << "import_files -force -norecurse\n";
  os << "update_compile_order -fileset sources_1\n";
  os << "\n";

  // Set top-level modules.
  os << "set_property top " << m_module_name << " [current_fileset]\n";
  os << "set_property top " << m_module_name << "_tb [get_filesets sim_1]\n";
  os << "update_compile_order -fileset sources_1\n";

  os << "exit\n";
  os.flush();
  return !os.has_error();
}

bool ProjectGenerator::WriteFIFOComponent()
{
  static const char fifo_vhdl[] = R"(library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity fifo is
  generic (
    constant DATA_WIDTH : positive := 8;
    constant SIZE : positive := 16
  );
  port (
    clk : in std_logic;
    rst_n : in std_logic;
    write : in std_logic;
    read : in std_logic;
    empty_n : out std_logic;
    full_n : out std_logic;
    din : in std_logic_vector(DATA_WIDTH - 1 downto 0);
    dout : out std_logic_vector(DATA_WIDTH - 1 downto 0)
  );
end fifo;

architecture behav of fifo is
begin

main_proc : process(clk)
  type Storage is array(0 to SIZE - 1) of std_logic_vector(DATA_WIDTH - 1 downto 0);
  variable Memory : Storage;
  
  variable head_ptr : natural range 0 to SIZE - 1;
  variable tail_ptr : natural range 0 to SIZE - 1;
  variable looped : boolean;
    
begin
  if (rising_edge(clk)) then
    if (rst_n = '0') then
      head_ptr := 0;
      tail_ptr := 0;
      looped := false;
      full_n <= '1';
      empty_n <= '0';
    else
      if (read = '1') then
        if ((looped = true) or (head_ptr /= tail_ptr)) then
          if (tail_ptr = SIZE - 1) then
            tail_ptr := 0;
            looped := false;
          else
            tail_ptr := tail_ptr + 1;
          end if;
        end if;
      end if;
      
      if (write = '1') then
        if ((looped = false) or (head_ptr /= tail_ptr)) then
          Memory(head_ptr) := din;
          if (head_ptr = SIZE - 1) then
            head_ptr := 0;
            looped := true;
          else
            head_ptr := head_ptr + 1;
          end if;
        end if;
      end if;
      
      dout <= Memory(tail_ptr);
      
      if (head_ptr = tail_ptr) then
        if (looped) then
          full_n <= '0';
        else
          empty_n <= '0';
        end if;
      else
        full_n <= '1';
        empty_n <= '1';
      end if;
    end if;
  end if;
end process;
   
end behav;
)";

  static const char fifo_srl16_vhdl[] = R"(
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity fifo_srl16 is
  generic (
    constant DATA_WIDTH : positive := 8;
    constant DEPTH : integer := 16
  );
  port (
    clk : in std_logic;
    rst_n : in std_logic;
    write : in std_logic;
    read : in std_logic;
    empty_n : out std_logic;
    full_n : out std_logic;
    din : in std_logic_vector(DATA_WIDTH - 1 downto 0);
    dout : out std_logic_vector(DATA_WIDTH - 1 downto 0)
  );
end fifo_srl16;

architecture behav of fifo_srl16 is
  type ram_type is array(DEPTH - 1 downto 0) of std_logic_vector(DATA_WIDTH - 1 downto 0);
  signal fifo_storage    : ram_type := (others => (others => '0'));
  signal fifo_index_i    : signed (4 downto 0) := to_signed(-1, 5);
  signal fifo_empty      : boolean;
  signal fifo_full       : boolean;
  signal fifo_in_enable  : boolean;
  signal fifo_out_enable : boolean;
  
begin
  fifo_full       <= (fifo_index_i = DEPTH-1);  
  fifo_empty      <= (fifo_index_i = -1);
   
  full_n          <= '1' when (not  fifo_full) else '0';
  empty_n         <= '1' when (not fifo_empty) else '0';
  
  fifo_in_enable  <= (write = '1') and (not fifo_full);
  fifo_out_enable <= (read = '1') and (not fifo_empty);
  
  dout            <= fifo_storage(to_integer(unsigned(fifo_index_i(3 downto 0))));  

  process(clk)
  begin
    if (rising_edge(clk)) then
      if (rst_n = '0') then
        fifo_index_i <= to_signed(-1, 5);
      else
        if (fifo_in_enable) then
          fifo_storage(DEPTH - 1 downto 1) <= fifo_storage(DEPTH - 2 downto 0);
          fifo_storage(0)                 <= din;
          if (not fifo_out_enable) then
            fifo_index_i <= fifo_index_i + 1;
          end if;
        elsif (fifo_out_enable) then
          fifo_index_i <= fifo_index_i - 1;
        end if;
      end if;
    end if;
  end process;
end behav;
)";

  static const char fifo_srl32_vhdl[] = R"(
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity fifo_srl32 is
  generic (
    constant DATA_WIDTH : positive := 8;
    constant DEPTH : integer := 32
  );
  port (
    clk : in std_logic;
    rst_n : in std_logic;
    write : in std_logic;
    read : in std_logic;
    empty_n : out std_logic;
    full_n : out std_logic;
    din : in std_logic_vector(DATA_WIDTH - 1 downto 0);
    dout : out std_logic_vector(DATA_WIDTH - 1 downto 0)
  );
end fifo_srl32;

architecture behav of fifo_srl32 is
  type ram_type is array(DEPTH - 1 downto 0) of std_logic_vector(DATA_WIDTH - 1 downto 0);
  signal fifo_storage    : ram_type := (others => (others => '0'));
  signal fifo_index_i    : signed (5 downto 0) := to_signed(-1, 6);
  signal fifo_empty      : boolean;
  signal fifo_full       : boolean;
  signal fifo_in_enable  : boolean;
  signal fifo_out_enable : boolean;
  
begin
  fifo_full       <= (fifo_index_i = DEPTH-1);  
  fifo_empty      <= (fifo_index_i = -1);
   
  full_n          <= '1' when (not  fifo_full) else '0';
  empty_n         <= '1' when (not fifo_empty) else '0';
  
  fifo_in_enable  <= (write = '1') and (not fifo_full);
  fifo_out_enable <= (read = '1') and (not fifo_empty);
  
  dout            <= fifo_storage(to_integer(unsigned(fifo_index_i(4 downto 0))));  

  process(clk)
  begin
    if (rising_edge(clk)) then
      if (rst_n = '0') then
        fifo_index_i <= to_signed(-1, 6);
      else
        if (fifo_in_enable) then
          fifo_storage(DEPTH - 1 downto 1) <= fifo_storage(DEPTH - 2 downto 0);
          fifo_storage(0)                 <= din;
          if (not fifo_out_enable) then
            fifo_index_i <= fifo_index_i + 1;
          end if;
        elsif (fifo_out_enable) then
          fifo_index_i <= fifo_index_i - 1;
        end if;
      end if;
    end if;
  end process;
end behav;
)";

  auto WriteFile = [this](const char* name, const char* data, size_t len) {
    std::string filename = StringFromFormat("%s/_autogen_vhdl/%s.vhd", m_output_dir.c_str(), name);
    Log_InfoPrintf("Writing FIFO component to %s...", filename.c_str());

    std::error_code ec;
    llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);
    if (ec || os.has_error())
      return false;

    os.write(data, len);
    os.flush();
    return true;
  };

  return (WriteFile("fifo", fifo_vhdl, sizeof(fifo_vhdl) - 1) &&
          WriteFile("fifo_srl16", fifo_srl16_vhdl, sizeof(fifo_srl16_vhdl) - 1) &&
          WriteFile("fifo_srl32", fifo_srl32_vhdl, sizeof(fifo_srl32_vhdl) - 1));
}

} // namespace HLSTarget
