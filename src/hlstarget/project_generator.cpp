#include "hlstarget/project_generator.h"
#include <cassert>
#include <map>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "hlstarget/filter_builder.h"
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
    Log::Info("ProjectGenerator", "IR prior to optimization");
    m_context->DumpModule(m_module);
  }

  OptimizeModule();

  if (debug_opt)
  {
    Log::Info("ProjectGenerator", "IR after optimization");
    m_context->DumpModule(m_module);
  }

  return true;
}

bool ProjectGenerator::GenerateProject()
{
  if (!CleanOutputDirectory())
  {
    Log::Error("ProjectGenerator", "Failed to clean output directory.");
    return false;
  }

  if (!WriteCCode())
  {
    Log::Error("ProjectGenerator", "Failed to write C code.");
    return false;
  }

  if (!WriteHLSScript())
  {
    Log::Error("ProjectGenerator", "Failed to write HLS script.");
    return false;
  }

  return true;
}

void ProjectGenerator::CreateModule()
{
  m_module = m_context->CreateModule(m_module_name.c_str());
  Log::Info("HLSProjectGenerator", "Module name is '%s'", m_module_name.c_str());
}

bool ProjectGenerator::GenerateFilterFunctions()
{
  Log::Info("ProjectGenerator", "Generating filter and channel functions...");

  auto filter_list = m_streamgraph->GetFilterPermutationList();
  for (const auto& it : filter_list)
  {
    const AST::FilterDeclaration* filter_decl = it.first;
    Log::Info("HLSTarget::ProjectGenerator", "Generating filter function for %s (%s)", filter_decl->GetName().c_str(),
              it.second.c_str());

    FilterBuilder fb(m_context, m_module, filter_decl);
    if (!fb.GenerateCode())
      return false;

    auto res = m_filter_function_map.emplace(it.second, fb.GetFunction());
    assert(res.second);
  }

  return true;
}

void ProjectGenerator::OptimizeModule()
{
  Log::Info("ProjectGenerator", "Optimizing LLVM IR...");

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
  //   Log::Info("ProjectGenerator", "Cleaning output directory '%s'...", m_output_dir.c_str());
  //   auto ec = llvm::sys::fs::remove_directories(m_output_dir);
  //   if (ec)
  //   {
  //     Log::Error("ProjectGenerator", "Failed to remove output directory '%s'", m_output_dir.c_str());
  //     return false;
  //   }

  std::error_code ec;
  if ((ec = llvm::sys::fs::create_directory(m_output_dir)) ||
      (ec = llvm::sys::fs::create_directory(StringFromFormat("%s/hls", m_output_dir.c_str()))))
  {
    Log::Error("ProjectGenerator", "Failed to create output directory '%s'", m_output_dir.c_str());
    return false;
  }

  return true;
}

bool ProjectGenerator::WriteCCode()
{
  std::string c_code_filename = StringFromFormat("%s/hls/filters.c", m_output_dir.c_str());
  Log::Info("ProjectGenerator", "Writing C code to %s...", c_code_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(c_code_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  llvm::legacy::PassManager pm;
  addCBackendPasses(pm, os);
  pm.run(*m_module);
  return true;
}

bool ProjectGenerator::WriteHLSScript()
{
  std::string script_filename = StringFromFormat("%s/hls/script.tcl", m_output_dir.c_str());
  Log::Info("ProjectGenerator", "Writing HLS TCL script to %s...", script_filename.c_str());

  std::error_code ec;
  llvm::raw_fd_ostream os(script_filename, ec, llvm::sys::fs::F_None);
  if (ec || os.has_error())
    return false;

  os << "open_project -reset " << m_module_name << "\n";
  os << "add_files filters.c\n";
  os << "\n";

  for (const auto& it : m_filter_function_map)
  {
    const std::string& filter_name = it.first;
    os << "open_solution -reset \"filter_" << filter_name << "\"\n";
    os << "set_top filter_" << filter_name << "\n";
    os << "set_part {xa7a50tcsg325-2i} -tool vivado\n";
    os << "create_clock -period 10 -name default\n";
    os << "csynth_design\n";
    os << "\n";
  }

  os << "exit\n";
  os.flush();
  return !os.has_error();
}

} // namespace HLSTarget
