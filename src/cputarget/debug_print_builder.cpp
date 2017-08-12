#include "cputarget/debug_print_builder.h"
#include "core/wrapped_llvm_context.h"
#include "llvm/IR/Module.h"

namespace CPUTarget
{
void BuildDebugPrint(WrappedLLVMContext* context, llvm::IRBuilder<>& builder, const char* msg)
{
  llvm::Module* mod = builder.GetInsertBlock()->getParent()->getParent();
  llvm::FunctionType* func_ty = llvm::FunctionType::get(context->GetVoidType(), {context->GetStringType()}, false);
  llvm::Constant* func = mod->getOrInsertFunction("streamit_debug_printf", func_ty);
  if (!func)
    return;

  builder.CreateCall(func, {builder.CreateGlobalStringPtr(msg)});
}

void BuildDebugPrintf(WrappedLLVMContext* context, llvm::IRBuilder<>& builder, const char* fmt,
                      const std::vector<llvm::Value*>& args)
{
  llvm::Module* mod = builder.GetInsertBlock()->getParent()->getParent();
  llvm::FunctionType* func_ty = llvm::FunctionType::get(context->GetVoidType(), {context->GetStringType()}, true);
  llvm::Constant* func = mod->getOrInsertFunction("streamit_debug_printf", func_ty);
  if (!func)
    return;

  std::vector<llvm::Value*> real_args;
  real_args.push_back(builder.CreateGlobalStringPtr(fmt));
  if (!args.empty())
    real_args.insert(real_args.end(), args.begin(), args.end());
  builder.CreateCall(func, real_args);
}
}