#include "frontend/wrapped_llvm_context.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include "common/log.h"
#include "common/string_helpers.h"
#include "frontend/constant_expression_builder.h"
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
#include "parser/ast.h"
Log_SetChannel(WrappedLLVMContext);

namespace Frontend
{
WrappedLLVMContext::WrappedLLVMContext() : m_llvm_context(std::make_unique<llvm::LLVMContext>())
{
}

WrappedLLVMContext::~WrappedLLVMContext()
{
}

llvm::Type* WrappedLLVMContext::GetVoidType()
{
  return llvm::Type::getVoidTy(*m_llvm_context);
}

llvm::Type* WrappedLLVMContext::GetByteType()
{
  return llvm::Type::getInt8Ty(*m_llvm_context);
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

llvm::Value* WrappedLLVMContext::CreateHostPointerValue(const void* ptr)
{
  llvm::Constant* cons = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*m_llvm_context),
                                                static_cast<uint64_t>(reinterpret_cast<intptr_t>(ptr)));
  llvm::Value* addr =
    llvm::ConstantExpr::getIntToPtr(cons, llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(*m_llvm_context)));
  return addr;
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

llvm::Type* WrappedLLVMContext::GetLLVMType(const AST::TypeSpecifier* type_specifier)
{
  llvm::Type* llvm_ty;
  if (type_specifier->IsPrimitiveType())
  {
    // Resolve base type -> LLVM type
    switch (type_specifier->GetTypeId())
    {
    case AST::TypeSpecifier::TypeId::Void:
      llvm_ty = llvm::Type::getVoidTy(*m_llvm_context);
      break;

    case AST::TypeSpecifier::TypeId::Boolean:
    case AST::TypeSpecifier::TypeId::Bit:
      llvm_ty = llvm::Type::getInt1Ty(*m_llvm_context);
      break;

    case AST::TypeSpecifier::TypeId::Int:
      llvm_ty = llvm::Type::getInt32Ty(*m_llvm_context);
      break;

    case AST::TypeSpecifier::TypeId::Float:
      llvm_ty = llvm::Type::getFloatTy(*m_llvm_context);
      break;

    case AST::TypeSpecifier::TypeId::APInt:
      llvm_ty = llvm::IntegerType::get(*m_llvm_context, type_specifier->GetNumBits());
      break;

    default:
      assert(0 && "unknown base type");
      return nullptr;
    }

    return llvm_ty;
  }

  if (type_specifier->IsStructType())
  {
    assert(0 && "TODO Implement structs");
    return nullptr;
  }

  if (type_specifier->IsArrayType())
  {
    // TODO: Pass "global variable" table for identifier expression resolving
    const AST::ArrayTypeSpecifier* array_type_specifier = static_cast<const AST::ArrayTypeSpecifier*>(type_specifier);

    // Resolve base type.
    llvm::Type* base_ty = GetLLVMType(array_type_specifier->GetBaseType());

    // Get the length of the array.
    ConstantExpressionBuilder ceb(this);
    if (!array_type_specifier->GetArrayDimensions()->Accept(&ceb) || !ceb.IsValid())
    {
      assert(0 && "failed to get constant expression for array size");
      return nullptr;
    }

    unsigned array_length;
    llvm::ConstantInt* CI = llvm::dyn_cast<llvm::ConstantInt>(ceb.GetResultValue());
    if (CI && CI->getBitWidth() <= 32)
    {
      array_length = static_cast<unsigned>(CI->getSExtValue());
    }
    else
    {
      assert(0 && "failed to get constant expression value type for array size");
      return nullptr;
    }

    return llvm::ArrayType::get(base_ty, array_length);
  }

  assert(0 && "Unknown type");
  return nullptr;
}

std::unique_ptr<WrappedLLVMContext> WrappedLLVMContext::Create()
{
  return std::make_unique<WrappedLLVMContext>();
}

llvm::Constant* WrappedLLVMContext::CreateConstantFromPointer(llvm::Type* ty, const void* ptr)
{
  const char* value_ptr = reinterpret_cast<const char*>(ptr);

  // If this is a pointer type, get the inner type first.
  if (ty->isPointerTy())
    return CreateConstantFromPointerInternal(ty->getPointerElementType(), value_ptr);
  else
    return CreateConstantFromPointerInternal(ty, value_ptr);
}

llvm::Constant* WrappedLLVMContext::CreateConstantFromPointerInternal(llvm::Type* ty, const char*& ptr)
{
  if (ty->isIntegerTy())
  {
    uint64_t value = 0;
    size_t size = (ty->getIntegerBitWidth() + 7) / 8;
    std::memcpy(&value, ptr, size);
    ptr += size;
    Log_DevPrintf("Int value readback: %08X %08X", unsigned(value >> 32), unsigned(value));
    return llvm::ConstantInt::get(ty, value);
  }

  if (ty->isFloatTy())
  {
    float value;
    std::memcpy(&value, ptr, sizeof(value));
    ptr += sizeof(value);
    Log_DevPrintf("Float value readback: %f", value);
    return llvm::ConstantFP::get(ty, double(value));
  }

  if (ty->isArrayTy())
  {
    llvm::ArrayType* array_ty = llvm::cast<llvm::ArrayType>(ty);
    llvm::Type* array_element_ty = array_ty->getArrayElementType();
    uint64_t num_values = array_ty->getArrayNumElements();
    std::vector<llvm::Constant*> array_values;
    for (uint64_t i = 0; i < num_values; i++)
      array_values.push_back(CreateConstantFromPointerInternal(array_element_ty, ptr));
    return llvm::ConstantArray::get(array_ty, array_values);
  }

  assert(0 && "unknown type");
  return nullptr;
}

WrappedLLVMContext::VariableMap* WrappedLLVMContext::GetTopVariableMap() const
{
  if (m_variable_map_stack.empty())
    return nullptr;

  return m_variable_map_stack.top();
}

void WrappedLLVMContext::PushVariableMap(VariableMap* vm)
{
  m_variable_map_stack.push(vm);
}

void WrappedLLVMContext::PopVariableMap()
{
  assert(!m_variable_map_stack.empty());
  m_variable_map_stack.pop();
}

} // namespace Frontend
