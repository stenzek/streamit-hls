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

static llvm::Value* GetRateValue(Frontend::FunctionBuilder* fb, AST::Expression* expr)
{
  // Use 0 when rate is unspecified.
  if (!expr)
    return llvm::ConstantInt::get(fb->GetContext()->GetIntType(), static_cast<uint64_t>(0));

  Frontend::ExpressionBuilder eb(fb);
  if (!expr->Accept(&eb) || !eb.IsValid())
    return nullptr;

  return eb.GetResultValue();
}

bool StreamGraphFunctionBuilder::Visit(AST::FilterDeclaration* node)
{
  llvm::Value* peek_rate_val = GetRateValue(this, node->GetWorkBlock()->GetPeekRateExpression());
  llvm::Value* pop_rate_val = GetRateValue(this, node->GetWorkBlock()->GetPopRateExpression());
  llvm::Value* push_rate_val = GetRateValue(this, node->GetWorkBlock()->GetPushRateExpression());
  if (!peek_rate_val || !pop_rate_val || !push_rate_val)
    return false;

  // We're a filter, simply call AddFilter
  // TODO: Handling of parameters - we would probably need to create a boxed type for each
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_AddFilter");
  GetCurrentIRBuilder().CreateCall(
    call_func, {m_context->CreateHostPointerValue(node), peek_rate_val, pop_rate_val, push_rate_val});
  GetCurrentIRBuilder().CreateRetVoid();
  return true;
}

bool StreamGraphFunctionBuilder::Visit(AST::SplitJoinDeclaration* node)
{
  // We're a splitjoin
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_BeginSplitJoin");
  GetCurrentIRBuilder().CreateCall(call_func, {m_context->CreateHostPointerValue(node)});

  // Generate statements
  bool result = node->GetStatements()->Accept(this);

  // End of splitjoin
  call_func = GetModule()->getFunction("StreamGraphBuilder_EndSplitJoin");
  GetCurrentIRBuilder().CreateCall(call_func);
  GetCurrentIRBuilder().CreateRetVoid();
  return result;
}

bool StreamGraphFunctionBuilder::Visit(AST::PipelineDeclaration* node)
{
  // We're a pipeline
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_BeginPipeline");
  GetCurrentIRBuilder().CreateCall(call_func, {m_context->CreateHostPointerValue(node)});

  // Generate statements
  bool result = node->GetStatements()->Accept(this);

  // End of splitjoin
  call_func = GetModule()->getFunction("StreamGraphBuilder_EndPipeline");
  GetCurrentIRBuilder().CreateCall(call_func);
  GetCurrentIRBuilder().CreateRetVoid();
  return result;
}
}