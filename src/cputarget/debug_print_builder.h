#pragma once
#include <vector>
#include "llvm/IR/IRBuilder.h"

class WrappedLLVMContext;

namespace llvm
{
class BasicBlock;
class Constant;
class Function;
class Module;
}

namespace CPUTarget
{
void BuildDebugPrint(WrappedLLVMContext* context, llvm::IRBuilder<>& builder, const char* msg);
void BuildDebugPrintf(WrappedLLVMContext* context, llvm::IRBuilder<>& builder, const char* fmt,
                      const std::vector<llvm::Value*>& args);
}
