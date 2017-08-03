#include "frontend/context.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include "common/log.h"
#include "frontend/filter_builder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FormattedStream.h"
#include "parser/ast.h"
#include "parser/parser_state.h"
#include "parser/type.h"

namespace Frontend
{

Context::Context()
{
}

Context::~Context()
{
}

llvm::Type* Context::GetLLVMType(const Type* type)
{
  auto it = m_type_map.find(type);
  if (it != m_type_map.end())
    return it->second;

  llvm::Type* ty = CreateLLVMType(type);
  m_type_map.emplace(type, ty);
  return ty;
}

llvm::Type* Context::GetVoidType()
{
  return llvm::Type::getVoidTy(m_llvm_context);
}

llvm::Type* Context::GetIntType()
{
  return llvm::Type::getInt32Ty(m_llvm_context);
}

llvm::Type* Context::GetIntPtrType()
{
  return llvm::Type::getInt32PtrTy(m_llvm_context);
}

llvm::Type* Context::GetStringType()
{
  return llvm::Type::getInt8PtrTy(m_llvm_context);
}

llvm::Type* Context::GetPointerType()
{
  // clang seems to represent void* as i8*
  return llvm::Type::getInt8PtrTy(m_llvm_context);
}

llvm::Module* Context::CreateModule(const char* name)
{
  return new llvm::Module(name, m_llvm_context);
}

void Context::DestroyModule(llvm::Module* mod)
{
  delete mod;
}

void Context::DumpModule(llvm::Module* mod)
{
  // TODO: Can we use the modern PassManager?
  llvm::legacy::PassManager pm;
  pm.add(llvm::createPrintModulePass(llvm::outs()));
  pm.run(*mod);
}

bool Context::VerifyModule(llvm::Module* mod)
{
  // validate module, should this be here or elsewhere?
  // verifyModule returns true if there are errors, false otherwise
  return !llvm::verifyModule(*mod, &llvm::outs());
}

unsigned int Context::GenerateNameId()
{
  return m_id_counter++;
}

std::string Context::GenerateName(const char* prefix)
{
  return StringFromFormat("%s_%u", prefix, m_id_counter);
}

void Context::LogError(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Error("frontend", "%s", msg.c_str());
}

void Context::LogWarning(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Warning("frontend", "%s", msg.c_str());
}

void Context::LogInfo(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Info("frontend", "%s", msg.c_str());
}

void Context::LogDebug(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  std::string msg = StringFromFormatV(fmt, ap);
  va_end(ap);
  Log::Debug("frontend", "%s", msg.c_str());
}

llvm::Type* Context::CreateLLVMType(const Type* type)
{
  llvm::Type* llvm_ty;
  if (!type->HasStructBase())
  {
    // Resolve base type -> LLVM type
    switch (type->GetBaseTypeId())
    {
    case Type::BaseTypeId::Boolean:
    case Type::BaseTypeId::Bit:
      llvm_ty = llvm::Type::getInt1Ty(m_llvm_context);
      break;

    case Type::BaseTypeId::Int:
      llvm_ty = llvm::Type::getInt32Ty(m_llvm_context);
      break;

    case Type::BaseTypeId::Float:
      llvm_ty = llvm::Type::getDoubleTy(m_llvm_context);
      break;

    default:
      assert(0 && "unknown base type");
      return nullptr;
    }
  }
  else
  {
    assert(0 && "TODO Implement structs");
    return nullptr;
  }

  // If it's not an array, we're done with the mapping
  if (!type->IsArrayType())
    return llvm_ty;

  // Work in reverse, so int[10][5][2] would be 2 int[10][5]s, which are 5 int[10]s, which are 10 ints.
  for (auto it = type->GetArraySizes().rbegin(); it != type->GetArraySizes().rend(); it++)
    llvm_ty = llvm::ArrayType::get(llvm_ty, *it);

  return llvm_ty;
}
}

#if 0
bool temp_codegenerator_run(ParserState* state)
{
  Frontend::Context cg;

  bool result = true;
  for (AST::FilterDeclaration* filter_decl : state->GetFilterList())
  {
    Frontend::FilterBuilder fb(&cg, cg.GetModule(), filter_decl);
    result &= fb.GenerateCode();
  }

  result &= cg.VerifyModule();

  cg.DumpModule();

  return result;
}
#endif