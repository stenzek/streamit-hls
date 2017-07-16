#include "frontend/context.h"
#include "frontend/filter_builder.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FormattedStream.h"
#include "parser/ast.h"
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

bool Context::GenerateCode(AST::Program* program)
{
  m_module = new llvm::Module("program", m_llvm_context);

  // TODO: This will eventually use the stream graph..
  for (AST::FilterDeclaration* filter_decl : program->GetFilterList())
  {
    FilterBuilder fb(this, m_module, filter_decl);
    if (!fb.GenerateCode())
      return false;
  }

  // TODO: Can we use the modern PassManager?
  llvm::legacy::PassManager pm;
  pm.add(llvm::createPrintModulePass(llvm::outs()));
  pm.run(*m_module);

  // validate module, should this be here or elsewhere?
  if (!llvm::verifyModule(*m_module, &llvm::outs()))
    return false;

  return true;
}
}

bool temp_codegenerator_run(AST::Program* program)
{
  Frontend::Context cg;
  return cg.GenerateCode(program);
}
