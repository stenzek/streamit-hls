#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

namespace llvm
{
class LLVMContext;
class Constant;
class Module;
class Type;
class Value;
}
namespace AST
{
class TypeSpecifier;
}

namespace Frontend
{
class WrappedLLVMContext
{
public:
  WrappedLLVMContext();
  ~WrappedLLVMContext();

  llvm::LLVMContext& GetLLVMContext() { return *m_llvm_context; }
  llvm::Type* GetLLVMType(const AST::TypeSpecifier* type_specifier);

  llvm::Type* GetVoidType();
  llvm::Type* GetByteType();
  llvm::Type* GetBooleanType();
  llvm::Type* GetIntType();
  llvm::Type* GetIntPtrType();
  llvm::Type* GetStringType();
  llvm::Type* GetPointerType();

  llvm::Value* CreateHostPointerValue(const void* ptr);

  llvm::Module* CreateModule(const char* name);
  void DestroyModule(llvm::Module* mod);
  void DumpModule(llvm::Module* mod);
  bool VerifyModule(llvm::Module* mod);

  unsigned int GenerateNameId();
  std::string GenerateName(const char* prefix);

  void LogError(const char* fmt, ...);
  void LogWarning(const char* fmt, ...);
  void LogInfo(const char* fmt, ...);
  void LogDebug(const char* fmt, ...);

  // void BuildDebugPrint(llvm::IRBuilder<>& builder, const char* msg);
  // void BuildDebugPrintf(llvm::IRBuilder<>& builder, const char* fmt, const std::vector<llvm::Value*>& args);

  static std::unique_ptr<WrappedLLVMContext> Create();
  static llvm::Constant* CreateConstantFromPointer(llvm::Type* ty, const void* ptr);

private:
  static llvm::Constant* CreateConstantFromPointerInternal(llvm::Type* ty, const char*& ptr);

  std::unique_ptr<llvm::LLVMContext> m_llvm_context;
  int m_id_counter = 1;
};
} // namespace Frontend