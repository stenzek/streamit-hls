#include "core/wrapped_llvm_context.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FormattedStream.h"

WrappedLLVMContext::WrappedLLVMContext() : m_llvm_context(std::make_unique<llvm::LLVMContext>())
{
}

WrappedLLVMContext::~WrappedLLVMContext()
{
}

llvm::Type* WrappedLLVMContext::GetLLVMType(const Type* type)
{
  auto it = m_type_map.find(type);
  if (it != m_type_map.end())
    return it->second;

  llvm::Type* ty = CreateLLVMType(type);
  m_type_map.emplace(type, ty);
  return ty;
}

llvm::Type* WrappedLLVMContext::GetVoidType()
{
  return llvm::Type::getVoidTy(*m_llvm_context);
}

llvm::Type* WrappedLLVMContext::GetBooleanType()
{
  return llvm::Type::getInt1Ty(*m_llvm_context);
}

llvm::Type* WrappedLLVMContext::GetIntType()
{
  return llvm::Type::getInt32Ty(*m_llvm_context);
}

llvm::Type* WrappedLLVMContext::GetIntPtrType()
{
  return llvm::Type::getInt32PtrTy(*m_llvm_context);
}

llvm::Type* WrappedLLVMContext::GetStringType()
{
  return llvm::Type::getInt8PtrTy(*m_llvm_context);
}

llvm::Type* WrappedLLVMContext::GetPointerType()
{
  // clang seems to represent void* as i8*
  return llvm::Type::getInt8PtrTy(*m_llvm_context);
}

llvm::Module* WrappedLLVMContext::CreateModule(const char* name)
{
  return new llvm::Module(name, *m_llvm_context);
}

void WrappedLLVMContext::DestroyModule(llvm::Module* mod)
{
  delete mod;
}

void WrappedLLVMContext::DumpModule(llvm::Module* mod)
{
  // TODO: Can we use the modern PassManager?
  llvm::legacy::PassManager pm;
  pm.add(llvm::createPrintModulePass(llvm::outs()));
  pm.run(*mod);
}

bool WrappedLLVMContext::VerifyModule(llvm::Module* mod)
{
  // validate module, should this be here or elsewhere?
  // verifyModule returns true if there are errors, false otherwise
  return !llvm::verifyModule(*mod, &llvm::outs());
}

unsigned int WrappedLLVMContext::GenerateNameId()
{
  return m_id_counter++;
}

std::string WrappedLLVMContext::GenerateName(const char* prefix)
{
  return StringFromFormat("%s_%u", prefix, m_id_counter);
}

void WrappedLLVMContext::LogError(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Error("frontend", "%s", msg.c_str());
}

void WrappedLLVMContext::LogWarning(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Warning("frontend", "%s", msg.c_str());
}

void WrappedLLVMContext::LogInfo(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Info("frontend", "%s", msg.c_str());
}

void WrappedLLVMContext::LogDebug(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Debug("frontend", "%s", msg.c_str());
}

llvm::Type* WrappedLLVMContext::CreateLLVMType(const Type* type)
{
  llvm::Type* llvm_ty;
  if (type->IsPrimitiveType())
  {
    // Resolve base type -> LLVM type
    switch (type->GetTypeId())
    {
    case Type::TypeId::Void:
      llvm_ty = llvm::Type::getVoidTy(*m_llvm_context);
      break;

    case Type::TypeId::Boolean:
    case Type::TypeId::Bit:
      llvm_ty = llvm::Type::getInt1Ty(*m_llvm_context);
      break;

    case Type::TypeId::Int:
      llvm_ty = llvm::Type::getInt32Ty(*m_llvm_context);
      break;

    case Type::TypeId::Float:
      llvm_ty = llvm::Type::getDoubleTy(*m_llvm_context);
      break;

    default:
      assert(0 && "unknown base type");
      return nullptr;
    }

    return llvm_ty;
  }

  if (type->IsStructType())
  {
    assert(0 && "TODO Implement structs");
    return nullptr;
  }

  if (type->IsArrayType())
  {
    // Get base type
    auto array_ty = static_cast<const ArrayType*>(type);
    llvm_ty = GetLLVMType(array_ty->GetBaseType());

    // Work in reverse, so int[10][5][2] would be 2 int[10][5]s, which are 5 int[10]s, which are 10 ints.
    for (auto it = array_ty->GetArraySizes().rbegin(); it != array_ty->GetArraySizes().rend(); it++)
      llvm_ty = llvm::ArrayType::get(llvm_ty, *it);

    return llvm_ty;
  }

  assert(0 && "Unknown type");
  return nullptr;
}

std::unique_ptr<WrappedLLVMContext> WrappedLLVMContext::Create()
{
  return std::make_unique<WrappedLLVMContext>();
}
