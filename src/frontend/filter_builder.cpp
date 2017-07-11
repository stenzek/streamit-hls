#include "frontend/filter_builder.h"
#include <cassert>
#include "frontend/filter_function_builder.h"
#include "frontend/context.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"

namespace Frontend
{

FilterBuilder::FilterBuilder(Context* context, llvm::Module* module, AST::FilterDeclaration* filter_decl)
  : m_context(context), m_module(module), m_filter_decl(filter_decl)
{
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode()
{
  // TODO: This should use filter instances, not filters
  if (m_filter_decl->HasInitBlock())
  {
    std::string name = StringFromFormat("%s_init", m_filter_decl->GetName().c_str());
    m_init_function = GenerateFunction(m_filter_decl->GetInitBlock(), name);
    if (!m_init_function)
      return false;
  }

  if (m_filter_decl->HasPreworkBlock())
  {
    std::string name = StringFromFormat("%s_prework", m_filter_decl->GetName().c_str());
    m_prework_function = GenerateFunction(m_filter_decl->GetPreworkBlock(), name);
    if (!m_prework_function)
      return false;
  }

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("%s_work", m_filter_decl->GetName().c_str());
    m_work_function = GenerateFunction(m_filter_decl->GetWorkBlock(), name);
    if (!m_work_function)
      return false;
  }

  return true;
}

llvm::Function* FilterBuilder::GenerateFunction(AST::FilterWorkBlock* block, const std::string& name)
{
  assert(m_module->getFunction(name.c_str()) == nullptr);
  llvm::Type* ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), ret_type, nullptr);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return nullptr;

  // Start at the entry basic block for the work function.
  FilterFunctionBuilder entry_bb_builder(m_context, "entry", func);
  if (!block->Accept(&entry_bb_builder))
    return nullptr;

  return func;
}

} // namespace Frontend