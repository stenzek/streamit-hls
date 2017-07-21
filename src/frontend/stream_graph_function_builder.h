#pragma once
#include "frontend/filter_function_builder.h"

namespace Frontend
{
class StreamGraphFunctionBuilder : public FilterFunctionBuilder
{
public:
  StreamGraphFunctionBuilder(Context* ctx, llvm::Module* mod, const std::string& name, llvm::Function* func);
  ~StreamGraphFunctionBuilder();

  bool Visit(AST::FilterDeclaration* node) override;
  bool Visit(AST::SplitJoinDeclaration* node) override;
  bool Visit(AST::PipelineDeclaration* node) override;
};
}