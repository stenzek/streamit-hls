#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include "common/log.h"
#include "core/wrapped_llvm_context.h"
#include "hlstarget/project_generator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Signals.h"
#include "parser/ast.h"
#include "parser/ast_printer.h"
#include "parser/parser_state.h"
#include "parser/symbol_table.h"
#include "streamgraph/streamgraph.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static std::unique_ptr<ParserState> ParseFile(WrappedLLVMContext* ctx, const char* filename, std::FILE* fp, bool debug);
static void DumpAST(ParserState* parser);

static std::unique_ptr<StreamGraph::StreamGraph> GenerateStreamGraph(WrappedLLVMContext* ctx, ParserState* parser);
static void DumpStreamGraph(StreamGraph::StreamGraph* streamgraph);

static std::unique_ptr<HLSTarget::ProjectGenerator> GenerateCode(WrappedLLVMContext* ctx, ParserState* parser,
                                                                 StreamGraph::StreamGraph* streamgraph,
                                                                 const std::string& dirname);
static void DumpModule(WrappedLLVMContext* ctx, llvm::Module* mod);
static bool GenerateProject(WrappedLLVMContext* ctx, HLSTarget::ProjectGenerator* generator);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void usage(const char* progname)
{
  fprintf(stderr, "usage: %s [-w projectdir] [-a] [-d] [-a] [-s] [-i] [-o] [-e] [-h]\n", progname);
  fprintf(stderr, "  -w: Write output project.\n");
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
  bool write_project = false;

  int c;

  while ((c = getopt(argc, argv, "dasioehw:")) != -1)
  {
    switch (c)
    {
    case 'w':
      output_filename = optarg;
      write_project = true;
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

  std::unique_ptr<HLSTarget::ProjectGenerator> generator =
    GenerateCode(llvm_context.get(), parser.get(), streamgraph.get(), output_filename);
  if (!generator)
    return EXIT_FAILURE;

  if (dump_llvm_ir)
    DumpModule(llvm_context.get(), generator->GetModule());

  if (write_project)
  {
    if (!GenerateProject(llvm_context.get(), generator.get()))
      return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<ParserState> ParseFile(WrappedLLVMContext* ctx, const char* filename, std::FILE* fp, bool debug)
{
  Log::Info("HLSCompiler", "Parsing %s...", filename);

  auto parser = std::make_unique<ParserState>();
  if (!parser->ParseFile(filename, fp, debug))
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

std::unique_ptr<HLSTarget::ProjectGenerator> GenerateCode(WrappedLLVMContext* ctx, ParserState* parser,
                                                          StreamGraph::StreamGraph* streamgraph,
                                                          const std::string& dirname)
{
  Log::Info("HLSCompiler", "Generating code...");

  auto builder = std::make_unique<HLSTarget::ProjectGenerator>(ctx, streamgraph, parser->GetEntryPointName(), dirname);
  if (!builder->GenerateCode())
  {
    Log::Error("HLSCompiler", "Code generation failed.");
    return nullptr;
  }

  // Verify the LLVM bitcode. Can skip this.
  if (!ctx->VerifyModule(builder->GetModule()))
  {
    Log::Warning("HLSCompiler", "LLVM IR failed validation.");
    return nullptr;
  }

  return std::move(builder);
}

void DumpModule(WrappedLLVMContext* ctx, llvm::Module* mod)
{
  Log::Info("HLSCompiler", "Dumping LLVM IR...");
  ctx->DumpModule(mod);
}

bool GenerateProject(WrappedLLVMContext* ctx, HLSTarget::ProjectGenerator* generator)
{
  Log::Info("HLSCompiler", "Writing project to %s...", generator->GetOutputDirectoryName().c_str());
  if (generator->GetOutputDirectoryName().length() <= 1)
  {
    Log::Error("HLSCompiler", "Not writing project with a short output directory.");
    return false;
  }

  if (!generator->GenerateProject())
  {
    Log::Error("HLSCompiler", "Failed to generate project.");
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
