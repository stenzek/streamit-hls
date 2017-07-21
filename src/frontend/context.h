#pragma once
#include <memory>
#include <unordered_map>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

namespace llvm
{
class Constant;
class Module;
class Type;
class Value;
}
namespace AST
{
class FilterDeclaration;
}
class Type;

namespace Frontend
{
class Context
{
public:
  Context();
  ~Context();

  llvm::LLVMContext& GetLLVMContext() { return m_llvm_context; }
  llvm::Type* GetLLVMType(const Type* type);

  llvm::Type* GetVoidType();
  llvm::Type* GetIntType();
  llvm::Type* GetStringType();
  llvm::Type* GetPointerType();

  std::unique_ptr<llvm::Module> CreateModule(const char* name);
  void DumpModule(llvm::Module* mod);
  bool VerifyModule(llvm::Module* mod);

  unsigned int GenerateNameId();
  std::string GenerateName(const char* prefix);

  void LogError(const char* fmt, ...);
  void LogWarning(const char* fmt, ...);
  void LogInfo(const char* fmt, ...);
  void LogDebug(const char* fmt, ...);

private:
  llvm::Type* CreateLLVMType(const Type* type);

  llvm::LLVMContext m_llvm_context;

  using TypeMap = std::unordered_map<const Type*, llvm::Type*>;
  TypeMap m_type_map;

  int m_id_counter = 1;
};
}
