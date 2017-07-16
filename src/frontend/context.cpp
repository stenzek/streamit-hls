#include "frontend/context.h"
#include "frontend/filter_builder.h"
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
  // TODO: Improve this, support arrays.
  if (type->GetName() == "int")
    return llvm::Type::getInt32Ty(m_llvm_context);
  if (type->GetName() == "boolean")
    return llvm::Type::getInt1Ty(m_llvm_context);
  // return llvm::Type::getPrimitiveType(&m_llvm_context, llvm::Type::IntTyID);

  assert(0 && "unknown type");
  return nullptr;
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
