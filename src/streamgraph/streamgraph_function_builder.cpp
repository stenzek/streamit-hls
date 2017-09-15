#include "streamgraph/streamgraph_function_builder.h"
#include <cassert>
#include "frontend/expression_builder.h"
#include "frontend/statement_builder.h"
#include "frontend/wrapped_llvm_context.h"
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

StreamGraphFunctionBuilder::StreamGraphFunctionBuilder(Frontend::WrappedLLVMContext* ctx, llvm::Module* mod,
                                                       llvm::Function* func)
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

static void AddStreamParameterValues(Frontend::FunctionBuilder* fb, AST::StreamDeclaration* stream_decl,
                                     std::vector<llvm::Value*>& call_params)
{
  for (AST::ParameterDeclaration* param_decl : *stream_decl->GetParameters())
    call_params.push_back(fb->GetVariable(param_decl));
}

bool StreamGraphFunctionBuilder::Visit(AST::FilterDeclaration* node)
{
  llvm::Value* peek_rate_val = GetRateValue(this, node->GetWorkBlock()->GetPeekRateExpression());
  llvm::Value* pop_rate_val = GetRateValue(this, node->GetWorkBlock()->GetPopRateExpression());
  llvm::Value* push_rate_val = GetRateValue(this, node->GetWorkBlock()->GetPushRateExpression());
  if (!peek_rate_val || !pop_rate_val || !push_rate_val)
    return false;

  // We're a filter, simply call AddFilter
  llvm::Function* call_func = GetModule()->getFunction("StreamGraphBuilder_AddFilter");
  std::vector<llvm::Value*> call_params = {m_context->CreateHostPointerValue(node), peek_rate_val, pop_rate_val,
                                           push_rate_val};
  AddStreamParameterValues(this, node, call_params);
  GetCurrentIRBuilder().CreateCall(call_func, call_params);
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