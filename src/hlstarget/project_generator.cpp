#include "hlstarget/project_generator.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "hlstarget/component_generator.h"
#include "hlstarget/component_test_bench_generator.h"
#include "hlstarget/filter_builder.h"
#include "hlstarget/test_bench_generator.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
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
ProjectGenerator::ProjectGenerator(WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
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

    auto res = m_filter_function_map.emplace(filter_perm, fb.GetFunction());
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
  builder.addExtension(llvm::PassManagerBuilder::EP_LoopOptimizerEnd,
                       [](const llvm::PassManagerBuilder& builder, llvm::legacy::PassManagerBase& pm) {
                         pm.add(llvm::createLoopUnrollPass(-1 /* threshold */, -1 /* count */, -1 /* allowpartial */,
                                                           -1 /* runtime */, -1 /* upperbound */));
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
    const std::string& filter_name = filter_perm->GetName();
    const std::string function_name = StringFromFormat("filter_%s", filter_name.c_str());
    os << "# filter " << filter_name << "\n"
       << "open_solution -reset \"" << function_name << "\"\n"
       << "set_top " << function_name << "\n"
       << "set_part {xa7a50tcsg325-2i} -tool vivado\n"
       << "create_clock -period 10 -name default\n"
       << "\n";

    os << "# directives\n";

    // Since all our static variables are "state", they must be initialized at reset.
    // We don't use statics for any data storage, except for the FIFO peek buffer.
    // TODO: We can optimize this by setting them specifically instead of globally,
    // set_directive_reset "filter_counter" counter_last
    os << "config_rtl -reset state -reset_level low\n";

    // Disable handshake signals on block, we don't need them, since use the fifo for control
    os << "set_directive_interface -mode ap_ctrl_none \"" << function_name << "\"\n";

    // Make input pointer a fifo
    if (!filter_perm->GetInputType()->IsVoid())
    {
      u32 depth = std::max(filter_perm->GetPeekRate(), filter_perm->GetPopRate());
      os << "set_directive_interface -mode ap_fifo -depth " << depth << " \"" << function_name
         << "\" llvm_cbe_in_ptr\n";
    }

    // Make output pointer a fifo
    if (!filter_perm->GetOutputType()->IsVoid())
    {
      os << "set_directive_interface -mode ap_fifo -depth " << filter_perm->GetPushRate() << " \"" << function_name
         << "\" llvm_cbe_out_ptr\n";
    }

    os << "\n";

    os << "# commands\n";
    os << "csynth_design\n";
    os << "csim_design -argv \"" << function_name << "\"\n";
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

  os << "create_project " << m_module_name << " . -part xa7a50tcsg325 -force\n";
  os << "set_property target_language VHDL [current_project]\n";
  os << "\n";

  // Add HLS output files.
  // There can be more than one VHDL file per HLS solution.
  os << "# Add HLS outputs\n";
  for (const auto& it : m_filter_function_map)
  {
    const StreamGraph::FilterPermutation* filter_perm = it.first;
    const std::string& filter_name = filter_perm->GetName();
    const std::string function_name = StringFromFormat("filter_%s", filter_name.c_str());
    os << "add_files -norecurse [glob ./_autogen_hls/" << m_module_name << "/" << function_name << "/syn/vhdl/*.vhd]\n";
  }
  os << "\n";

  // Add wrapper component.
  os << "add_files -norecurse \"./_autogen_vhdl/fifo.vhd\"\n";
  os << "add_files -norecurse \"./_autogen_vhdl/" << m_module_name << ".vhd"
     << "\"\n";
  os << "add_files -norecurse \"./_autogen_vhdl/" << m_module_name << "_tb.vhd"
     << "\"\n";
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

  std::string filename = StringFromFormat("%s/_autogen_vhdl/fifo.vhd", m_output_dir.c_str());
  Log_InfoPrintf("Writing FIFO component to %s...", filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  os.write(fifo_vhdl, sizeof(fifo_vhdl) - 1);
  os.flush();
  return true;
}

} // namespace HLSTarget
