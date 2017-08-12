#include "streamgraph/streamgraph_function_builder.h"
#include <cassert>
#include "core/wrapped_llvm_context.h"
#include "frontend/expression_builder.h"
#include "frontend/statement_builder.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"

namespace StreamGraph
{
// Dummy interface for push/pop/peek
struct StreamGraphTargetFragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
  StreamGraphTargetFragmentBuilder() = default;
  ~StreamGraphTargetFragmentBuilder() = default;

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final { return nullptr; }
  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final { return nullptr; }
  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final { return false; }
};

StreamGraphFunctionBuilder::StreamGraphFunctionBuilder(WrappedLLVMContext* ctx, llvm::Module* mod, llvm::Function* func)
  : Frontend::FunctionBuilder(ctx, mod, new StreamGraphTargetFragmentBuilder(), func)
{
}

StreamGraphFunctionBuilder::~StreamGraphFunctionBuilder()
{
  delete m_target_builder;
  m_target_builder = nullptr;
}

bool StreamGraphFunctionBuilder::Visit(AST::FilterDeclaration* node)
{
  // We're a filter, simply call AddFilter
  // TODO: Handling of parameters - we would probably need to create a boxed type for each
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_AddFilter");
  llvm::Value* filter_name_ptr = GetCurrentIRBuilder().CreateGlobalStringPtr(node->GetName().c_str());
  assert(call_func);
  GetCurrentIRBuilder().CreateCall(call_func, filter_name_ptr);
  GetCurrentIRBuilder().CreateRetVoid();
  return true;
}

bool StreamGraphFunctionBuilder::Visit(AST::SplitJoinDeclaration* node)
{
  // We're a splitjoin
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_BeginSplitJoin");
  llvm::Value* name_ptr = GetCurrentIRBuilder().CreateGlobalStringPtr(node->GetName().c_str());
  assert(call_func);
  GetCurrentIRBuilder().CreateCall(call_func, name_ptr);

  // Generate statements
  bool result = node->GetStatements()->Accept(this);

  // End of splitjoin
  call_func = GetModule()->getFunction("StreamGraphBuilder_EndSplitJoin");
  assert(call_func);
  GetCurrentIRBuilder().CreateCall(call_func, name_ptr);
  GetCurrentIRBuilder().CreateRetVoid();
  return result;
}

bool StreamGraphFunctionBuilder::Visit(AST::PipelineDeclaration* node)
{
  // We're a pipeline
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_BeginPipeline");
  llvm::Value* name_ptr = GetCurrentIRBuilder().CreateGlobalStringPtr(node->GetName().c_str());
  assert(call_func);
  GetCurrentIRBuilder().CreateCall(call_func, name_ptr);

  // Generate statements
  bool result = node->GetStatements()->Accept(this);

  // End of splitjoin
  call_func = GetModule()->getFunction("StreamGraphBuilder_EndPipeline");
  assert(call_func);
  GetCurrentIRBuilder().CreateCall(call_func, name_ptr);
  GetCurrentIRBuilder().CreateRetVoid();
  return result;
}
}