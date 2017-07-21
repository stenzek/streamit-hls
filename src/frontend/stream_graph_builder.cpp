#include "frontend/stream_graph_builder.h"
#include <cassert>
#include <iostream>
#include "common/log.h"
#include "common/string_helpers.h"
#include "frontend/context.h"
#include "frontend/stream_graph.h"
#include "frontend/stream_graph_function_builder.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "parser/ast.h"
#include "parser/parser_state.h"
#include "parser/type.h"

namespace Frontend
{
StreamGraphBuilder::StreamGraphBuilder(Context* context, ParserState* state) : m_context(context), m_parser_state(state)
{
  m_module = m_context->CreateModule("streamgraph");
}

StreamGraphBuilder::~StreamGraphBuilder()
{
}

bool StreamGraphBuilder::GenerateGraph()
{
  if (!GenerateCode())
    return false;

  m_context->DumpModule(m_module.get());
  if (!m_context->VerifyModule(m_module.get()))
    m_context->LogError("Module verification failed");

  m_context->LogInfo("Creating execution engine");
  if (!CreateExecutionEngine())
    return false;

  m_context->LogInfo("Executing main");
  ExecuteMain();

  return true;
}

bool StreamGraphBuilder::GenerateCode()
{
  if (!GenerateGlobals() || !GenerateStreamGraphFunctions())
    return false;

  bool result = true;

  // We need to generate prototypes for everything first, since they can have Add statements
  for (auto* filter : m_parser_state->GetFilterList())
    result &= GenerateStreamFunctionPrototype(filter);
  for (auto* stream : m_parser_state->GetStreamList())
    result &= GenerateStreamFunctionPrototype(stream);

  // Now we can create the actual bodies of the functions
  if (result)
  {
    // Filters first, then splitjoins/pipelines
    // TODO: Combine these lists?
    for (auto* filter : m_parser_state->GetFilterList())
      result &= GenerateStreamFunction(filter);
    for (auto* stream : m_parser_state->GetStreamList())
      result &= GenerateStreamFunction(stream);
  }

  if (!GenerateMain())
    return false;

  return result;
}

bool StreamGraphBuilder::GenerateGlobals()
{
  return true;
}

bool StreamGraphBuilder::GenerateStreamGraphFunctions()
{
  m_module->getOrInsertFunction("StreamGraphBuilder_BeginPipeline", m_context->GetVoidType(),
                                m_context->GetStringType(), nullptr);
  m_module->getOrInsertFunction("StreamGraphBuilder_EndPipeline", m_context->GetVoidType(), m_context->GetStringType(),
                                nullptr);
  m_module->getOrInsertFunction("StreamGraphBuilder_BeginSplitJoin", m_context->GetVoidType(),
                                m_context->GetStringType(), nullptr);
  m_module->getOrInsertFunction("StreamGraphBuilder_EndSplitJoin", m_context->GetVoidType(), m_context->GetStringType(),
                                nullptr);
  m_module->getOrInsertFunction("StreamGraphBuilder_Split", m_context->GetVoidType(), m_context->GetIntType(), nullptr);
  m_module->getOrInsertFunction("StreamGraphBuilder_Join", m_context->GetVoidType(), nullptr);
  m_module->getOrInsertFunction("StreamGraphBuilder_AddFilter", m_context->GetVoidType(), m_context->GetStringType(),
                                nullptr);
  return true;
}

bool StreamGraphBuilder::GenerateStreamFunctionPrototype(AST::StreamDeclaration* decl)
{
  // Generate a function for each filter/splitjoin/pipeline in the program
  // These functions will call the runtime StreamGraph methods
  // TODO: Work out how to handle stream parameters.. varargs?
  std::string name = StringFromFormat("%s_add", decl->GetName().c_str());
  llvm::Type* ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), ret_type, nullptr);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  assert(func_cons && func);

  m_function_map.emplace(decl, func);
  return true;
}

bool StreamGraphBuilder::GenerateStreamFunction(AST::StreamDeclaration* decl)
{
  auto iter = m_function_map.find(decl);
  assert(iter != m_function_map.end());
  m_context->LogDebug("Generating stream function for %s", decl->GetName().c_str());

  StreamGraphFunctionBuilder builder(m_context, m_module.get(), decl->GetName(), iter->second);
  return decl->Accept(&builder);
}

bool StreamGraphBuilder::GenerateMain()
{
  AST::Node* entry_point_decl = m_parser_state->GetGlobalLexicalScope()->GetName(m_parser_state->GetEntryPointName());
  if (!entry_point_decl)
    return false;

  // This might be a bad cast, but if it is, it won't return a valid iterator.
  auto it = m_function_map.find(static_cast<AST::StreamDeclaration*>(entry_point_decl));
  if (it == m_function_map.end())
    return false;

  // TODO: Verify parameters. The entry point shouldn't have any.
  // This probably should be done in the semantic analysis.

  // Create main prototype
  llvm::Type* ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  llvm::Constant* func_cons = m_module->getOrInsertFunction("main", ret_type, nullptr);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);

  // Create main body - a single call to the entry point
  llvm::BasicBlock* bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(bb);
  builder.CreateCall(it->second);
  builder.CreateRetVoid();
  return true;
}

bool StreamGraphBuilder::CreateExecutionEngine()
{
  // TODO: Inline all function calls, to propogate as many constants as possible
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string error_msg;
  m_execution_engine = llvm::EngineBuilder(std::move(m_module)).setErrorStr(&error_msg).create();

  if (!m_execution_engine)
  {
    m_context->LogError("Failed to create LLVM execution engine: %s", error_msg.c_str());
    return false;
  }

  return true;
}

void StreamGraphBuilder::ExecuteMain()
{
  llvm::Function* main_func = m_execution_engine->FindFunctionNamed("main");
  assert(main_func && "main function exists in execution engine");

  m_execution_engine->runFunction(main_func, {});
}

} // namespace Frontend

// TODO: Move this elsewhere
#if defined(_WIN32) || defined(__CYGWIN__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

//////////////////////////////////////////////////////////////////////////
// Functions visible to generated code
//////////////////////////////////////////////////////////////////////////
extern "C" {
EXPORT void StreamGraphBuilder_BeginPipeline(const char* name)
{
  // Begin new pipeline
  Log::Debug("frontend", "StreamGraph BeginPipeline %s", name);
}
EXPORT void StreamGraphBuilder_EndPipeline(const char* name)
{
  // End pipeline and add to parent
  Log::Debug("frontend", "StreamGraph EndPipeline %s", name);
}
EXPORT void StreamGraphBuilder_BeginSplitJoin(const char* name)
{
  Log::Debug("frontend", "StreamGraph BeginSplitJoin %s", name);
}
EXPORT void StreamGraphBuilder_EndSplitJoin(const char* name)
{
  Log::Debug("frontend", "StreamGraph EndSplitJoin %s", name);
}
EXPORT void StreamGraphBuilder_Split(int mode)
{
  const char* mode_str = (mode == 0) ? "duplicate" : "roundrobin";
  Log::Debug("frontend", "StreamGraph Split %s", mode_str);
}
EXPORT void StreamGraphBuilder_Join()
{
  Log::Debug("frontend", "StreamGraph Join");
}
EXPORT void StreamGraphBuilder_AddFilter(const char* name)
{
  // Direct add to current
  Log::Debug("frontend", "StreamGraph AddFilter %s", name);
}
}
