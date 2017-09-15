#include "frontend/state_variables_builder.h"
#include <cassert>
#include "frontend/constant_expression_builder.h"
#include "frontend/wrapped_llvm_context.h"
#include "parser/ast.h"

namespace Frontend
{
StateVariablesBuilder::StateVariablesBuilder(WrappedLLVMContext* context_, llvm::Module* mod_,
                                             const std::string& prefix_)
  : m_context(context_), m_module(mod_), m_prefix(prefix_)
{
}

bool StateVariablesBuilder::Visit(AST::VariableDeclaration* node)
{
  llvm::Constant* initializer = nullptr;
  if (node->HasInitializer())
  {
    // The complex initializers should have already been moved to init()
    assert(node->GetInitializer()->IsConstant());

    // The types should be the same..
    assert(*node->GetInitializer()->GetType() == *node->GetType());

    // Translate to a LLVM constant
    ConstantExpressionBuilder ceb(m_context);
    if (node->HasInitializer() && (!node->GetInitializer()->Accept(&ceb) || !ceb.IsValid()))
      return false;

    initializer = ceb.GetResultValue();
  }

  // Generate the name for it
  std::string var_name = StringFromFormat("%s_%s", m_prefix.c_str(), node->GetName().c_str());

  // Create LLVM global var
  llvm::Type* llvm_ty = m_context->GetLLVMType(node->GetType());
  llvm::GlobalVariable* llvm_var =
    new llvm::GlobalVariable(*m_module, llvm_ty, true, llvm::GlobalValue::PrivateLinkage, initializer, var_name);

  if (initializer)
    llvm_var->setConstant(false);

  m_global_var_map.emplace(node, llvm_var);
  return true;
}

bool StateVariablesBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback handler executed");
  return false;
}
}