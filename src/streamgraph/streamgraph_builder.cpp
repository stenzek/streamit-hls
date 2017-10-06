#include "streamgraph/streamgraph_builder.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <iostream>
#include <memory>
#include <stack>
#include <vector>
#include "common/log.h"
#include "common/string_helpers.h"
#include "frontend/wrapped_llvm_context.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "parser/ast.h"
#include "parser/parser_state.h"
#include "streamgraph/streamgraph.h"
#include "streamgraph/streamgraph_function_builder.h"

static StreamGraph::BuilderState* s_builder_state;

namespace StreamGraph
{
Builder::Builder(Frontend::WrappedLLVMContext* context, ParserState* state) : m_context(context), m_parser_state(state)
{
  m_module = std::unique_ptr<llvm::Module>(m_context->CreateModule("streamgraph"));
}

Builder::~Builder()
{
}

std::unique_ptr<BuilderState> Builder::GenerateGraph()
{
  if (!GenerateCode())
    return nullptr;

  if (!m_context->VerifyModule(m_module.get()))
  {
    Log::Error("StreamGraphBuilder", "Stream graph module verification failed");
    m_context->DumpModule(m_module.get());
  }

  Log::Info("StreamGraphBuilder", "Creating execution engine");
  if (!CreateExecutionEngine())
    return nullptr;

  Log::Info("StreamGraphBuilder", "Executing main");
  ExecuteMain();

  if (m_builder_state->GetStartNode() == nullptr)
  {
    Log::Error("StreamGraphBuilder", "No root node found.");
    return nullptr;
  }

  return std::move(m_builder_state);
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
  m_module->getOrInsertFunction(
    "StreamGraphBuilder_BeginPipeline",
    llvm::FunctionType::get(m_context->GetVoidType(), {m_context->GetPointerType()}, false));
  m_module->getOrInsertFunction("StreamGraphBuilder_EndPipeline",
                                llvm::FunctionType::get(m_context->GetVoidType(), false));
  m_module->getOrInsertFunction(
    "StreamGraphBuilder_BeginSplitJoin",
    llvm::FunctionType::get(m_context->GetVoidType(), {m_context->GetPointerType()}, false));
  m_module->getOrInsertFunction("StreamGraphBuilder_EndSplitJoin",
                                llvm::FunctionType::get(m_context->GetVoidType(), false));
  m_module->getOrInsertFunction(
    "StreamGraphBuilder_Split",
    llvm::FunctionType::get(m_context->GetVoidType(), {m_context->GetIntType(), m_context->GetIntType()}, true));
  m_module->getOrInsertFunction("StreamGraphBuilder_Join",
                                llvm::FunctionType::get(m_context->GetVoidType(), {m_context->GetIntType()}, true));
  m_module->getOrInsertFunction("StreamGraphBuilder_AddFilter",
                                llvm::FunctionType::get(m_context->GetVoidType(),
                                                        {m_context->GetPointerType(), m_context->GetIntType(),
                                                         m_context->GetIntType(), m_context->GetIntType()},
                                                        true));
  return true;
}

bool Builder::GenerateStreamFunctionPrototype(AST::StreamDeclaration* decl)
{
  // Generate a function for each filter/splitjoin/pipeline in the program
  // These functions will call the runtime StreamGraph methods
  // TODO: Work out how to handle stream parameters.. varargs?
  std::string name = StringFromFormat("%s_add", decl->GetName().c_str());
  llvm::FunctionType* func_type = Frontend::FunctionBuilder::GetFunctionType(m_context, decl->GetParameters());
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), func_type);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  assert(func_cons && func);

  m_function_map.emplace(decl, func);
  return true;
}

bool Builder::GenerateStreamFunction(AST::StreamDeclaration* decl)
{
  auto iter = m_function_map.find(decl);
  assert(iter != m_function_map.end());
  Log::Debug("StreamGraphBuilder", "Generating stream function for %s", decl->GetName().c_str());

  StreamGraphFunctionBuilder builder(m_context, m_module.get(), iter->second);
  builder.CreateParameterVariables(decl->GetParameters());
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
    Log::Error("StreamGraphBuilder", "Failed to create LLVM execution engine: %s", error_msg.c_str());
    return false;
  }

  m_execution_engine->finalizeObject();
  return true;
}

void Builder::ExecuteMain()
{
  llvm::Function* main_func = m_execution_engine->FindFunctionNamed("main");
  assert(main_func && "main function exists in execution engine");

  // Setup static state.
  m_builder_state = std::make_unique<BuilderState>(m_context, m_parser_state);
  s_builder_state = m_builder_state.get();

  m_execution_engine->runFunction(main_func, {});

  // Clear static state.
  s_builder_state = nullptr;
}

BuilderState::BuilderState(Frontend::WrappedLLVMContext* context, ParserState* state)
  : m_context(context), m_parser_state(state)
{
}

void BuilderState::ExtractParameters(FilterParameters* out_params, const AST::StreamDeclaration* stream_decl,
                                     va_list ap)
{
  for (const AST::ParameterDeclaration* param_decl : *stream_decl->GetParameters())
  {
    const AST::TypeSpecifier* ty = param_decl->GetType();

    // TODO: Handle array types here.
    assert(!ty->IsArrayType());
    if (ty->IsInt())
    {
      int data = va_arg(ap, int);
      llvm::Constant* value =
        llvm::ConstantInt::get(m_context->GetIntType(), static_cast<uint64_t>(static_cast<int64_t>(data)));
      out_params->AddParameter(param_decl, &data, sizeof(data), value);
    }
    else if (ty->IsBoolean())
    {
      bool data = (va_arg(ap, int)) ? true : false;
      llvm::Constant* value = llvm::ConstantInt::get(m_context->GetBooleanType(), static_cast<uint64_t>(data));
      out_params->AddParameter(param_decl, &data, sizeof(data), value);
    }
    else
    {
      assert(0 && "unknown type");
    }
  }
}

void BuilderState::AddFilter(const AST::FilterDeclaration* decl, int peek_rate, int pop_rate, int push_rate, va_list ap)
{
  if (!HasTopNode())
  {
    Error("Attempting to add filter %s to top-level node", decl->GetName().c_str());
    return;
  }

  FilterParameters filter_params;
  ExtractParameters(&filter_params, decl, ap);

  llvm::Type* filter_input_type = m_context->GetLLVMType(decl->GetInputType());
  llvm::Type* filter_output_type = m_context->GetLLVMType(decl->GetOutputType());

  // Find a matching permutation
  // This is where we would compare parameter values
  FilterPermutation* filter_perm;
  auto iter = std::find_if(m_filter_permutations.begin(), m_filter_permutations.end(), [&](const auto& it) {
    return (it->GetFilterDeclaration() == decl && it->GetFilterParameters() == filter_params);
  });
  if (iter != m_filter_permutations.end())
  {
    // These should match
    filter_perm = *iter;
    if (filter_perm->GetPeekRate() != peek_rate || filter_perm->GetPopRate() != pop_rate ||
        filter_perm->GetPushRate() != push_rate)
    {
      Error("Internal error, mismatched peek/push/pop rates for filter '%s'", decl->GetName().c_str());
      return;
    }
  }
  else
  {
    // Create new permutation
    std::string name = StringFromFormat("%s_%u", decl->GetName().c_str(), unsigned(m_filter_permutations.size() + 1));
    filter_perm = new FilterPermutation(name, decl, filter_params, filter_input_type, filter_output_type, peek_rate,
                                        pop_rate, push_rate, (pop_rate > 0) ? 1 : 0, (push_rate > 0) ? 1 : 0);
    m_filter_permutations.push_back(filter_perm);
  }

  std::string instance_name = GenerateName(decl->GetName());
  Filter* flt = new Filter(instance_name, filter_perm);
  if (!GetTopNode()->AddChild(this, flt))
    delete flt;

  // Is this the program input?
  if (decl->IsBuiltin() && std::strncmp(decl->GetName().c_str(), "InputReader__", 13) == 0)
  {
    if (m_program_input_node != nullptr)
    {
      Error("Program input already defined when adding filter '%s' (previous input was '%s')", instance_name.c_str(),
            m_program_input_node->GetName().c_str());
      return;
    }

    m_program_input_node = flt;
  }

  if (decl->IsBuiltin() && std::strncmp(decl->GetName().c_str(), "OutputWriter__", 14) == 0)
  {
    if (m_program_output_node != nullptr)
    {
      Error("Program output already defined when adding filter '%s' (previous output was '%s')", instance_name.c_str(),
            m_program_output_node->GetName().c_str());
      return;
    }

    m_program_output_node = flt;
  }
}

void BuilderState::BeginPipeline(const AST::PipelineDeclaration* decl)
{
  std::string instance_name = GenerateName(decl->GetName());
  Pipeline* p = new Pipeline(instance_name);
  m_node_stack.push(p);
}

void BuilderState::EndPipeline()
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

void BuilderState::BeginSplitJoin(const AST::SplitJoinDeclaration* decl)
{
  std::string instance_name = GenerateName(decl->GetName());
  SplitJoin* p = new SplitJoin(instance_name);
  m_node_stack.push(p);
}

void BuilderState::EndSplitJoin()
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

void BuilderState::SplitJoinSplit(int mode, const std::vector<int>& distribution)
{
  if (!HasTopNode())
  {
    Error("Attempt to add split as top-level node");
    return;
  }

  std::string instance_name = GenerateName("split");
  Split* split = new Split(instance_name, (mode == 0) ? Split::Mode::Duplicate : Split::Mode::Roundrobin, distribution);
  if (!GetTopNode()->AddChild(this, split))
    delete split;
}

void BuilderState::SplitJoinJoin(const std::vector<int>& distribution)
{
  if (!HasTopNode())
  {
    Error("Attempt to add join as top-level node");
    return;
  }

  std::string instance_name = GenerateName("join");
  Join* join = new Join(instance_name, distribution);
  if (!GetTopNode()->AddChild(this, join))
    delete join;
}

std::string BuilderState::GenerateName(const std::string& prefix)
{
  return StringFromFormat("%s_%u", prefix.c_str(), m_name_id++);
}

bool BuilderState::HasTopNode() const
{
  return !m_node_stack.empty();
}

Node* BuilderState::GetTopNode()
{
  assert(!m_node_stack.empty());
  return m_node_stack.top();
}

void BuilderState::Error(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  Log::Error("stream_graph", "%s", StringFromFormatV(fmt, ap).c_str());
  va_end(ap);
}

std::unique_ptr<StreamGraph> BuildStreamGraph(Frontend::WrappedLLVMContext* context, ParserState* parser)
{
  Builder builder(context, parser);
  auto builder_state = builder.GenerateGraph();
  if (!builder_state)
    return nullptr;

  builder_state->GetStartNode()->SteadySchedule();

  return std::make_unique<StreamGraph>(builder_state->GetStartNode(), builder_state->GetFilterPermutations(),
                                       builder_state->GetProgramInputNode(), builder_state->GetProgramOutputNode());
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
EXPORT void StreamGraphBuilder_BeginPipeline(const AST::PipelineDeclaration* pipeline)
{
  // Begin new pipeline
  Log::Debug("StreamGraphBuilder", "StreamGraph BeginPipeline %s", pipeline->GetName().c_str());
  s_builder_state->BeginPipeline(pipeline);
}
EXPORT void StreamGraphBuilder_EndPipeline(const intptr_t pipeline_ptr)
{
  // End pipeline and add to parent
  Log::Debug("StreamGraphBuilder", "StreamGraph EndPipeline");
  s_builder_state->EndPipeline();
}
EXPORT void StreamGraphBuilder_BeginSplitJoin(const AST::SplitJoinDeclaration* splitjoin)
{
  Log::Debug("StreamGraphBuilder", "StreamGraph BeginSplitJoin %s", splitjoin->GetName().c_str());
  s_builder_state->BeginSplitJoin(splitjoin);
}
EXPORT void StreamGraphBuilder_EndSplitJoin(const intptr_t splitjoin_ptr)
{
  Log::Debug("StreamGraphBuilder", "StreamGraph EndSplitJoin");
  s_builder_state->EndSplitJoin();
}
EXPORT void StreamGraphBuilder_Split(int mode, int num_args, ...)
{
  const char* mode_str = (mode == 0) ? "duplicate" : "roundrobin";
  Log::Debug("StreamGraphBuilder", "StreamGraph Split %s", mode_str);

  std::vector<int> distribution;
  va_list ap;
  va_start(ap, num_args);
  for (int i = 0; i < num_args; i++)
  {
    int val = va_arg(ap, int);
    distribution.push_back(val);
  }
  va_end(ap);

  s_builder_state->SplitJoinSplit(mode, distribution);
}
EXPORT void StreamGraphBuilder_Join(int num_args, ...)
{
  Log::Debug("StreamGraphBuilder", "StreamGraph Join");

  std::vector<int> distribution;
  va_list ap;
  va_start(ap, num_args);
  for (int i = 0; i < num_args; i++)
  {
    int val = va_arg(ap, int);
    distribution.push_back(val);
  }
  va_end(ap);

  s_builder_state->SplitJoinJoin(distribution);
}
EXPORT void StreamGraphBuilder_AddFilter(const AST::FilterDeclaration* filter, int peek_rate, int pop_rate,
                                         int push_rate, ...)
{
  // Direct add to current
  va_list ap;
  va_start(ap, push_rate);
  Log::Debug("StreamGraphBuilder", "StreamGraph AddFilter %s peek=%d pop=%d push=%d", filter->GetName().c_str(),
             peek_rate, pop_rate, push_rate);
  s_builder_state->AddFilter(filter, peek_rate, pop_rate, push_rate, ap);
  va_end(ap);
}
}
