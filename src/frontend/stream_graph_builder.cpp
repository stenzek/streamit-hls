#include "frontend/stream_graph_builder.h"
#include <cassert>
#include <iostream>
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
}

StreamGraphBuilder::~StreamGraphBuilder()
{
}

bool StreamGraphBuilder::GenerateGraph()
{
  if (!GenerateCode())
    return false;

  m_context->DumpModule();
  if (!m_context->VerifyModule())
    (void)0;

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
  //   // Pop/peek
  //   if (!m_filter_decl->GetInputType()->IsVoid())
  //   {
  //     llvm::Type* ret_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
  //     llvm::Type* llvm_peek_idx_ty = llvm::Type::getInt32Ty(m_context->GetLLVMContext());
  //     llvm::FunctionType* llvm_peek_fn = llvm::FunctionType::get(ret_ty, {llvm_peek_idx_ty}, false);
  //     llvm::FunctionType* llvm_pop_fn = llvm::FunctionType::get(ret_ty, false);
  //     m_peek_function = m_context->GetModule()->getOrInsertFunction(StringFromFormat("%s_peek",
  //     m_name_prefix.c_str()), llvm_peek_fn);
  //     m_pop_function = m_context->GetModule()->getOrInsertFunction(StringFromFormat("%s_pop", m_name_prefix.c_str()),
  //     llvm_pop_fn);
  //   }
  //
  //   // Push
  //   if (!m_filter_decl->GetOutputType()->IsVoid())
  //   {
  //     llvm::Type* llvm_ty = m_context->GetLLVMType(m_filter_decl->GetOutputType());
  //     llvm::Type* ret_ty = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  //     llvm::FunctionType* llvm_push_fn = llvm::FunctionType::get(ret_ty, {llvm_ty}, false);
  //     m_push_function = m_context->GetModule()->getOrInsertFunction(StringFromFormat("%s_push",
  //     m_name_prefix.c_str()), llvm_push_fn);
  //   }

  llvm::Type* void_ty = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  m_context->GetModule()->getOrInsertFunction("StreamGraphInterpreter_BeginPipeline", void_ty, nullptr);
  m_context->GetModule()->getOrInsertFunction("StreamGraphInterpreter_EndPipeline", void_ty, nullptr);
  m_context->GetModule()->getOrInsertFunction("StreamGraphInterpreter_BeginSplitJoin", void_ty, nullptr);
  m_context->GetModule()->getOrInsertFunction("StreamGraphInterpreter_EndSplitJoin", void_ty, nullptr);
  m_context->GetModule()->getOrInsertFunction("StreamGraphInterpreter_AddFilter", void_ty, nullptr);
  return true;
}

bool StreamGraphBuilder::GenerateStreamFunctionPrototype(AST::StreamDeclaration* decl)
{
  // Generate a function for each filter/splitjoin/pipeline in the program
  // These functions will call the runtime StreamGraph methods
  // TODO: Work out how to handle stream parameters.. varargs?
  std::string name = StringFromFormat("%s_add", decl->GetName().c_str());
  llvm::Type* ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  llvm::Constant* func_cons = m_context->GetModule()->getOrInsertFunction(name.c_str(), ret_type, nullptr);
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

  StreamGraphFunctionBuilder builder(m_context, decl->GetName(), iter->second);
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
  llvm::Constant* func_cons = m_context->GetModule()->getOrInsertFunction("main", ret_type, nullptr);
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
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  // TODO: We could use the JIT. Not like it would matter.
  // TODO: Clean up this unique_ptr mess..
  std::unique_ptr<llvm::Module> mod_ptr(m_context->GetModule());
  std::string error_msg;
  m_execution_engine = llvm::EngineBuilder(std::move(mod_ptr)).setErrorStr(&error_msg).create();

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
EXPORT void StreamGraphInterpreter_BeginPipeline()
{
  std::cerr << "BeginPipeline" << std::endl;
}
EXPORT void StreamGraphInterpreter_EndPipeline()
{
  std::cerr << "EndPipeline" << std::endl;
}
EXPORT void StreamGraphInterpreter_BeginSplitJoin()
{
  std::cerr << "BeginSplitJoin" << std::endl;
}
EXPORT void StreamGraphInterpreter_EndSplitJoin()
{
  std::cerr << "EndSplitJoin" << std::endl;
}
EXPORT void StreamGraphInterpreter_AddFilter(const char* name)
{
  std::cerr << "AddFilter" << std::endl;
}
}

bool temp_codegenerator_run(ParserState* state)
{
  Frontend::Context cg;

  bool result = true;
  Frontend::StreamGraphBuilder b(&cg, state);
  result &= b.GenerateGraph();

  return result;
}