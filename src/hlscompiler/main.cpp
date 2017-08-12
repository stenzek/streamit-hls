#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include "common/log.h"
#include "core/wrapped_llvm_context.h"
#include "hlstarget/program_builder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
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
static void WriteCFile(WrappedLLVMContext* ctx, llvm::Module* mod, const char* filename);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void usage(const char* progname)
{
  fprintf(stderr, "usage: %s [-w outfile] [-a] [-d] [-a] [-s] [-i] [-o] [-e] [-h]\n", progname);
  fprintf(stderr, "  -w: Write output C file.\n");
  fprintf(stderr, "  -d: Debug parser.\n");
  fprintf(stderr, "  -a: Dump abstract syntax tree.\n");
  fprintf(stderr, "  -s: Dump stream graph.\n");
  fprintf(stderr, "  -i: Dump LLVM IR.\n");
  fprintf(stderr, "  -o: Optimize LLVM IR.\n");
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
  bool write_c_file = false;

  int c;

  while ((c = getopt(argc, argv, "dasioehw:")) != -1)
  {
    switch (c)
    {
    case 'w':
      output_filename = optarg;
      write_c_file = true;
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
      Log::Error("HLSCompiler", "Failed to open file {}", filename);
      return EXIT_FAILURE;
    }
  }

  std::unique_ptr<WrappedLLVMContext> llvm_context = WrappedLLVMContext::Create();
  std::unique_ptr<ParserState> parser = ParseFile(llvm_context.get(), filename, fp);
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

  std::unique_ptr<llvm::Module> module = GenerateCode(llvm_context.get(), parser.get(), streamgraph.get());
  if (!module)
    return EXIT_FAILURE;

  if (dump_llvm_ir)
    DumpModule(llvm_context.get(), module.get());

  if (write_c_file)
    WriteCFile(llvm_context.get(), module.get(), output_filename.c_str());

  return EXIT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<ParserState> ParseFile(WrappedLLVMContext* ctx, const char* filename, std::FILE* fp)
{
  Log::Info("HLSCompiler", "Parsing %s...", filename);

  auto parser = std::make_unique<ParserState>();
  if (!parser->ParseFile(filename, fp))
  {
    Log::Error("HLSCompiler", "Parse failed.");
    return nullptr;
  }

  return std::move(parser);
}

void DumpAST(ParserState* parser)
{
  Log::Info("HLSCompiler", "Dumping AST...");
  parser->DumpAST();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<StreamGraph::StreamGraph> GenerateStreamGraph(WrappedLLVMContext* ctx, ParserState* parser)
{
  Log::Info("HLSCompiler", "Generating stream graph...");

  auto streamgraph = StreamGraph::BuildStreamGraph(ctx, parser);
  if (!streamgraph)
  {
    Log::Error("HLSCompiler", "Stream graph build failed.");
    return nullptr;
  }

  return std::move(streamgraph);
}

void DumpStreamGraph(StreamGraph::StreamGraph* streamgraph)
{
  Log::Info("HLSCompiler", "Dumping stream graph...");
  std::cout << streamgraph->Dump() << std::endl;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<llvm::Module> GenerateCode(WrappedLLVMContext* ctx, ParserState* parser,
                                           StreamGraph::StreamGraph* streamgraph)
{
  Log::Info("HLSCompiler", "Generating code...");

  HLSTarget::ProgramBuilder builder(ctx, parser->GetEntryPointName());
  if (!builder.GenerateCode(streamgraph))
  {
    Log::Error("HLSCompiler", "Code generation failed.");
    return nullptr;
  }

  // Verify the LLVM bitcode. Can skip this.
  if (!ctx->VerifyModule(builder.GetModule()))
  {
    Log::Warning("HLSCompiler", "LLVM IR failed validation.");
    return nullptr;
  }

  return builder.DetachModule();
}

void DumpModule(WrappedLLVMContext* ctx, llvm::Module* mod)
{
  Log::Info("HLSCompiler", "Dumping LLVM IR...");
  ctx->DumpModule(mod);
}

extern void addCBackendPasses(llvm::legacy::PassManagerBase& PM, llvm::raw_pwrite_stream& Out);

void WriteCFile(WrappedLLVMContext* ctx, llvm::Module* mod, const char* filename)
{
  Log::Info("HLSCompiler", "Writing C code to %s...", filename);

  std::error_code ec;
  llvm::raw_fd_ostream os(filename, ec, llvm::sys::fs::F_None);

  llvm::legacy::PassManager pm;
  addCBackendPasses(pm, os);
  pm.run(*mod);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
