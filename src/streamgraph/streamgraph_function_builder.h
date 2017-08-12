#pragma once
#include "frontend/function_builder.h"

namespace StreamGraph
{
class StreamGraphFunctionBuilder : public Frontend::FunctionBuilder
{
public:
  StreamGraphFunctionBuilder(WrappedLLVMContext* ctx, llvm::Module* mod, llvm::Function* func);
  ~StreamGraphFunctionBuilder();

  bool Visit(AST::FilterDeclaration* node) override;
  bool Visit(AST::SplitJoinDeclaration* node) override;
  bool Visit(AST::PipelineDeclaration* node) override;
};
}