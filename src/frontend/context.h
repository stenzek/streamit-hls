#pragma once
#include <memory>
#include <unordered_map>
#include "llvm/IR/LLVMContext.h"

namespace llvm
{
class Module;
class Type;
}
class Type;
namespace AST
{
class Program;
}

namespace Frontend
{
class Context
{
public:
  Context();
  ~Context();

  llvm::LLVMContext& GetLLVMContext()
  {
    return m_llvm_context;
  }
  llvm::Type* GetLLVMType(const Type* type);

  llvm::Module* GetModule() const
  {
    return m_module;
  }

  bool GenerateCode(AST::Program* program);

private:
  llvm::Type* CreateLLVMType(const Type* type);

  llvm::LLVMContext m_llvm_context;

  using TypeMap = std::unordered_map<const Type*, llvm::Type*>;
  TypeMap m_type_map;

  llvm::Module* m_module;
};
}
