#pragma once
#include <vector>
#include "llvm/IR/IRBuilder.h"

namespace Frontend
{
class WrappedLLVMContext;
}

namespace llvm
{
class BasicBlock;
class Constant;
class Function;
class Module;
}

namespace CPUTarget
{
void BuildDebugPrint(Frontend::WrappedLLVMContext* context, llvm::IRBuilder<>& builder, const char* msg);
void BuildDebugPrintf(Frontend::WrappedLLVMContext* context, llvm::IRBuilder<>& builder, const char* fmt,
                      const std::vector<llvm::Value*>& args);
}
