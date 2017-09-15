#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include "common/log.h"
#include "cputarget/program_builder.h"
#include "frontend/wrapped_llvm_context.h"
#include "llvm/Bitcode/BitcodeWriter.h"
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

static std::unique_ptr<ParserState> ParseFile(Frontend::WrappedLLVMContext* ctx, const char* filename, std::FILE* fp,
                                              bool debug);
static void DumpAST(ParserState* parser);

static std::unique_ptr<StreamGraph::StreamGraph> GenerateStreamGraph(Frontend::WrappedLLVMContext* ctx,
                                                                     ParserState* parser);
static void DumpStreamGraph(StreamGraph::StreamGraph* streamgraph);

static std::unique_ptr<llvm::Module> GenerateCode(Frontend::WrappedLLVMContext* ctx, ParserState* parser,
                                                  StreamGraph::StreamGraph* streamgraph, bool optimize);
static void DumpModule(Frontend::WrappedLLVMContext* ctx, llvm::Module* mod);
static void WriteModule(Frontend::WrappedLLVMContext* ctx, llvm::Module* mod, const char* filename);
static bool ExecuteModule(Frontend::WrappedLLVMContext* ctx, std::unique_ptr<llvm::Module> mod);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void usage(const char* progname)
{
  fprintf(stderr, "usage: %s [-w outfile] [-a] [-d] [-a] [-s] [-i] [-o] [-e] [-h]\n", progname);
  fprintf(stderr, "  -w: Write LLVM bitcode file.\n");
  fprintf(stderr, "  -d: Debug parser.\n");
  fprintf(stderr, "  -a: Dump abstract syntax tree.\n");
  fprintf(stderr, "  -s: Dump stream graph.\n");
  fprintf(stderr, "  -i: Dump LLVM IR.\n");
  fprintf(stderr, "  -o: Optimize LLVM IR.\n");
  fprintf(stderr, "  -e: Execute program after compilation.\n");
  fprintf(stderr, "  -h: Print this help message.\n");
  fprintf(stderr, "\n");
  std::exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  Log::SetConsoleOutputParams(true);

  std::string output_filename;
  bool debug_parser = false;
  bool dump_ast = false;
  bool dump_stream_graph = false;
  bool dump_llvm_ir = false;
  bool optimize_llvm_ir = false;
  bool write_llvm_ir = false;
  bool execute_program = false;

  int c;

  while ((c = getopt(argc, argv, "dasioehw:")) != -1)
  {
    switch (c)
    {
    case 'w':
      output_filename = optarg;
      write_llvm_ir = true;
      break;

    case 'd':
      debug_parser = true;
      break;

    case 'a':
      dump_ast = true;
      break;

    case 's':
      dump_stream_graph = true;
      break;

    case 'i':
      dump_llvm_ir = true;
      break;

    case 'o':
      optimize_llvm_ir = true;
      break;

    case 'e':
      execute_program = true;
      break;

    case 'h':
      usage(argv[0]);
      return EXIT_FAILURE;

    default:
      fprintf(stderr, "%s: unknown option: %c\n", argv[0], c);
      return EXIT_FAILURE;
    }
  }

  const char* filename = "stdin";
  std::FILE* fp = stdin;
  if (argc > optind)
  {
    filename = argv[optind];
    fp = std::fopen(filename, "r");
    if (!fp)
    {
      Log::Error("CPUCompiler", "Failed to open file {}", filename);
      return EXIT_FAILURE;
    }
  }

  std::unique_ptr<Frontend::WrappedLLVMContext> llvm_context = Frontend::WrappedLLVMContext::Create();
  std::unique_ptr<ParserState> parser = ParseFile(llvm_context.get(), filename, fp, debug_parser);
  std::fclose(fp);
  if (!parser)
    return EXIT_FAILURE;

  if (dump_ast)
    DumpAST(parser.get());

  std::unique_ptr<StreamGraph::StreamGraph> streamgraph = GenerateStreamGraph(llvm_context.get(), parser.get());
  if (!streamgraph)
    return EXIT_FAILURE;

  if (dump_stream_graph)
    DumpStreamGraph(streamgraph.get());

  std::unique_ptr<llvm::Module> module =
    GenerateCode(llvm_context.get(), parser.get(), streamgraph.get(), optimize_llvm_ir);
  if (!module)
    return EXIT_FAILURE;

  if (dump_llvm_ir)
    DumpModule(llvm_context.get(), module.get());

  if (write_llvm_ir)
    WriteModule(llvm_context.get(), module.get(), output_filename.c_str());

  if (execute_program)
    ExecuteModule(llvm_context.get(), std::move(module));

  return EXIT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<ParserState> ParseFile(Frontend::WrappedLLVMContext* ctx, const char* filename, std::FILE* fp,
                                       bool debug)
{
  Log::Info("CPUCompiler", "Parsing %s...", filename);

  auto parser = std::make_unique<ParserState>();
  if (!parser->ParseFile(filename, fp, debug))
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

std::unique_ptr<StreamGraph::StreamGraph> GenerateStreamGraph(Frontend::WrappedLLVMContext* ctx, ParserState* parser)
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

std::unique_ptr<llvm::Module> GenerateCode(Frontend::WrappedLLVMContext* ctx, ParserState* parser,
                                           StreamGraph::StreamGraph* streamgraph, bool optimize)
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

  if (optimize)
    builder.OptimizeModule();

  return builder.DetachModule();
}

void DumpModule(Frontend::WrappedLLVMContext* ctx, llvm::Module* mod)
{
  Log::Info("CPUCompiler", "Dumping LLVM IR...");
  ctx->DumpModule(mod);
}

void WriteModule(Frontend::WrappedLLVMContext* ctx, llvm::Module* mod, const char* filename)
{
  Log::Info("CPUCompiler", "Writing LLVM IR to %s...", filename);

  std::error_code ec;
  llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);
  llvm::WriteBitcodeToFile(mod, os);
  os.flush();
}

bool ExecuteModule(Frontend::WrappedLLVMContext* ctx, std::unique_ptr<llvm::Module> mod)
{
  Log::Info("CPUCompiler", "Executing program...");

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
