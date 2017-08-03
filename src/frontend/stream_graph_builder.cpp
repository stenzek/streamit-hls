#include "frontend/stream_graph_builder.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <iostream>
#include <memory>
#include <stack>
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

static std::unique_ptr<StreamGraph::BuilderState> s_builder_state;

namespace StreamGraph
{
Builder::Builder(Frontend::Context* context, ParserState* state) : m_context(context), m_parser_state(state)
{
  m_module = std::unique_ptr<llvm::Module>(m_context->CreateModule("streamgraph"));
}

Builder::~Builder()
{
}

bool Builder::GenerateGraph()
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

  if (!m_start_node)
  {
    m_context->LogError("No root node found.");
    return false;
  }

  return true;
}

bool Builder::GenerateCode()
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

bool Builder::GenerateGlobals()
{
  return true;
}

bool Builder::GenerateStreamGraphFunctions()
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

bool Builder::GenerateStreamFunctionPrototype(AST::StreamDeclaration* decl)
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

bool Builder::GenerateStreamFunction(AST::StreamDeclaration* decl)
{
  auto iter = m_function_map.find(decl);
  assert(iter != m_function_map.end());
  m_context->LogDebug("Generating stream function for %s", decl->GetName().c_str());

  Frontend::StreamGraphFunctionBuilder builder(m_context, m_module.get(), decl->GetName(), iter->second);
  return decl->Accept(&builder);
}

bool Builder::GenerateMain()
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

bool Builder::CreateExecutionEngine()
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

void Builder::ExecuteMain()
{
  llvm::Function* main_func = m_execution_engine->FindFunctionNamed("main");
  assert(main_func && "main function exists in execution engine");

  // Setup static state.
  s_builder_state = std::make_unique<BuilderState>(m_parser_state);

  m_execution_engine->runFunction(main_func, {});

  // Clear static state.
  m_start_node = s_builder_state->GetStartNode();
  s_builder_state.reset();
}

BuilderState::BuilderState(ParserState* state) : m_parser_state(state)
{
}

void BuilderState::AddFilter(const char* name)
{
  const auto& filter_list = m_parser_state->GetFilterList();
  auto iter = std::find_if(filter_list.begin(), filter_list.end(),
                           [name](const AST::FilterDeclaration* decl) { return (decl->GetName() == name); });
  if (iter == filter_list.end())
  {
    Error("Attempting to add an unknown filter: %s", name);
    return;
  }

  if (!HasTopNode())
  {
    Error("Attempting to add filter %s to top-level node", name);
    return;
  }

  AST::FilterDeclaration* decl = *iter;
  std::string instance_name = GenerateName(name);
  Filter* flt = new Filter(decl, instance_name);
  if (!GetTopNode()->AddChild(this, flt))
    delete flt;
}

void BuilderState::BeginPipeline(const char* name)
{
  std::string instance_name = GenerateName(name);
  Pipeline* p = new Pipeline(instance_name);
  m_node_stack.push(p);
}

void BuilderState::EndPipeline(const char* name)
{
  Pipeline* p = dynamic_cast<Pipeline*>(GetTopNode());
  assert(p && "top node is a pipeline");
  m_node_stack.pop();

  if (!p->Validate(this))
    Error("Pipeline %s failed validation", p->GetName().c_str());

  if (!HasTopNode())
  {
    // This is the start of the program.
    Log::Info("frontend", "Program starts at %s", p->GetName().c_str());
    assert(!m_start_node && "start node is null");
    m_start_node = p;
  }
  else
  {
    if (!GetTopNode()->AddChild(this, p))
      delete p;
  }
}

void BuilderState::BeginSplitJoin(const char* name)
{
  std::string instance_name = GenerateName(name);
  SplitJoin* p = new SplitJoin(instance_name);
  m_node_stack.push(p);
}

void BuilderState::EndSplitJoin(const char* name)
{
  SplitJoin* p = dynamic_cast<SplitJoin*>(GetTopNode());
  assert(p && "top node is a stream");
  m_node_stack.pop();

  if (!HasTopNode())
  {
    Error("Attempt to add splitjoin %s as top-level node", p->GetName().c_str());
    return;
  }

  if (!GetTopNode()->AddChild(this, p))
    delete p;
}

void BuilderState::Split(int mode)
{
  if (!HasTopNode())
  {
    Error("Attempt to add split as top-level node");
    return;
  }

  std::string instance_name = GenerateName("split");
  StreamGraph::Split* split = new StreamGraph::Split(instance_name);
  if (!GetTopNode()->AddChild(this, split))
    delete split;
}

void BuilderState::Join()
{
  if (!HasTopNode())
  {
    Error("Attempt to add join as top-level node");
    return;
  }

  std::string instance_name = GenerateName("join");
  StreamGraph::Join* join = new StreamGraph::Join(instance_name);
  if (!GetTopNode()->AddChild(this, join))
    delete join;
}

std::string BuilderState::GenerateName(const char* prefix)
{
  return StringFromFormat("%s_%u", prefix ? prefix : "", m_name_id++);
}

bool BuilderState::HasTopNode() const
{
  return !m_node_stack.empty();
}

StreamGraph::Node* BuilderState::GetTopNode()
{
  assert(!m_node_stack.empty());
  return m_node_stack.top();
}

void BuilderState::Error(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  Log::Error("frontend", "%s", StringFromFormatV(fmt, ap).c_str());
  va_end(ap);
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
  s_builder_state->BeginPipeline(name);
}
EXPORT void StreamGraphBuilder_EndPipeline(const char* name)
{
  // End pipeline and add to parent
  Log::Debug("frontend", "StreamGraph EndPipeline %s", name);
  s_builder_state->EndPipeline(name);
}
EXPORT void StreamGraphBuilder_BeginSplitJoin(const char* name)
{
  Log::Debug("frontend", "StreamGraph BeginSplitJoin %s", name);
  s_builder_state->BeginSplitJoin(name);
}
EXPORT void StreamGraphBuilder_EndSplitJoin(const char* name)
{
  Log::Debug("frontend", "StreamGraph EndSplitJoin %s", name);
  s_builder_state->EndSplitJoin(name);
}
EXPORT void StreamGraphBuilder_Split(int mode)
{
  const char* mode_str = (mode == 0) ? "duplicate" : "roundrobin";
  Log::Debug("frontend", "StreamGraph Split %s", mode_str);
  s_builder_state->Split(mode);
}
EXPORT void StreamGraphBuilder_Join()
{
  Log::Debug("frontend", "StreamGraph Join");
  s_builder_state->Join();
}
EXPORT void StreamGraphBuilder_AddFilter(const char* name)
{
  // Direct add to current
  Log::Debug("frontend", "StreamGraph AddFilter %s", name);
  s_builder_state->AddFilter(name);
}
}
