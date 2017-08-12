#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include "common/log.h"
#include "core/wrapped_llvm_context.h"
#include "cputarget/program_builder.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
#include "streamgraph/streamgraph.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static std::unique_ptr<ParserState> ParseFile(WrappedLLVMContext* ctx, const char* filename, std::FILE* fp);
static void DumpAST(ParserState* parser);

static std::unique_ptr<StreamGraph::StreamGraph> GenerateStreamGraph(WrappedLLVMContext* ctx, ParserState* parser);
static void DumpStreamGraph(StreamGraph::StreamGraph* streamgraph);

static std::unique_ptr<llvm::Module> GenerateCode(WrappedLLVMContext* ctx, ParserState* parser,
                                                  StreamGraph::StreamGraph* streamgraph);
static void DumpModule(WrappedLLVMContext* ctx, llvm::Module* mod);
static bool ExecuteModule(WrappedLLVMContext* ctx, std::unique_ptr<llvm::Module> mod);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
  const char* filename = "stdin";
  std::FILE* fp = stdin;

  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  Log::SetConsoleOutputParams(true);

  if (argc > 1)
  {
    filename = argv[1];
    fp = std::fopen(filename, "r");
    if (!fp)
    {
      Log::Error("CPUCompiler", "Failed to open file {}", filename);
      return EXIT_FAILURE;
    }
  }

  std::unique_ptr<WrappedLLVMContext> llvm_context = WrappedLLVMContext::Create();
  std::unique_ptr<ParserState> parser = ParseFile(llvm_context.get(), filename, fp);
  std::fclose(fp);
  if (!parser)
    return EXIT_FAILURE;

  DumpAST(parser.get());

  std::unique_ptr<StreamGraph::StreamGraph> streamgraph = GenerateStreamGraph(llvm_context.get(), parser.get());
  if (!streamgraph)
    return EXIT_FAILURE;

  DumpStreamGraph(streamgraph.get());

  std::unique_ptr<llvm::Module> module = GenerateCode(llvm_context.get(), parser.get(), streamgraph.get());
  if (!module)
    return EXIT_FAILURE;

  DumpModule(llvm_context.get(), module.get());
  return EXIT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<ParserState> ParseFile(WrappedLLVMContext* ctx, const char* filename, std::FILE* fp)
{
  Log::Info("CPUCompiler", "Parsing %s...", filename);

  auto parser = std::make_unique<ParserState>();
  if (!parser->ParseFile(filename, fp))
  {
    Log::Error("CPUCompiler", "Parse failed.");
    return nullptr;
  }

  return std::move(parser);
}

void DumpAST(ParserState* parser)
{
  Log::Info("CPUCompiler", "Dumping AST...");
  parser->DumpAST();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<StreamGraph::StreamGraph> GenerateStreamGraph(WrappedLLVMContext* ctx, ParserState* parser)
{
  Log::Info("CPUCompiler", "Generating stream graph...");

  auto streamgraph = StreamGraph::BuildStreamGraph(ctx, parser);
  if (!streamgraph)
  {
    Log::Error("CPUCompiler", "Stream graph build failed.");
    return nullptr;
  }

  return std::move(streamgraph);
}

void DumpStreamGraph(StreamGraph::StreamGraph* streamgraph)
{
  Log::Info("CPUCompiler", "Dumping stream graph...");
  std::cout << streamgraph->Dump() << std::endl;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<llvm::Module> GenerateCode(WrappedLLVMContext* ctx, ParserState* parser,
                                           StreamGraph::StreamGraph* streamgraph)
{
  Log::Info("CPUCompiler", "Generating code...");

  CPUTarget::ProgramBuilder builder(ctx, parser->GetEntryPointName());
  if (!builder.GenerateCode(streamgraph))
  {
    Log::Error("CPUCompiler", "Code generation failed.");
    return nullptr;
  }

  // Verify the LLVM bitcode. Can skip this.
  if (!ctx->VerifyModule(builder.GetModule()))
  {
    Log::Warning("CPUCompiler", "LLVM IR failed validation.");
    return nullptr;
  }

  return builder.DetachModule();
}

void DumpModule(WrappedLLVMContext* ctx, llvm::Module* mod)
{
  Log::Info("CPUCompiler", "Dumping LLVM IR...");
  ctx->DumpModule(mod);
}

bool ExecuteModule(WrappedLLVMContext* ctx, std::unique_ptr<llvm::Module> mod)
{
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string error_msg;
  llvm::ExecutionEngine* execution_engine = llvm::EngineBuilder(std::move(mod)).setErrorStr(&error_msg).create();

  if (!execution_engine)
  {
    Log::Error("CPUCompiler", "Failed to create LLVM execution engine: %s", error_msg.c_str());
    return false;
  }

  execution_engine->finalizeObject();

  llvm::Function* main_func = execution_engine->FindFunctionNamed("main");
  assert(main_func && "main function exists in execution engine");
  Log::Info("CPUCompiler", "Executing main function...");
  execution_engine->runFunction(main_func, {});
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
