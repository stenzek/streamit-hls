#include "frontend/stream_graph_function_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/expression_builder.h"
#include "frontend/filter_builder.h"
#include "frontend/statement_builder.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"

namespace Frontend
{
StreamGraphFunctionBuilder::StreamGraphFunctionBuilder(Context* ctx, llvm::Module* mod, const std::string& name,
                                                       llvm::Function* func)
  : FilterFunctionBuilder(ctx, mod, nullptr, name, func)
{
}

StreamGraphFunctionBuilder::~StreamGraphFunctionBuilder()
{
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