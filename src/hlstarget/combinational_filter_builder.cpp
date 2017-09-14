#include "hlstarget/combinational_filter_builder.h"
#include <cassert>
#include <memory>
#include "common/log.h"
#include "common/string_helpers.h"
#include "common/types.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "frontend/constant_expression_builder.h"
#include "frontend/function_builder.h"
#include "frontend/state_variables_builder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(HLSTarget::CombinationalFilterBuilder);

namespace HLSTarget
{
struct CombinationalFragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
  CombinationalFragmentBuilder(llvm::Value* in_value) : m_in_value(in_value) {}

  llvm::AllocaInst* GetOutputValue() const { return m_out_value; }

  void CreateDeclarations(WrappedLLVMContext* context, llvm::IRBuilder<>& builder, llvm::Type* output_type)
  {
    if (!output_type->isVoidTy())
      m_out_value = builder.CreateAlloca(output_type, nullptr, "out_value");
  }

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final { return m_in_value; }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final { return nullptr; }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    builder.CreateStore(value, m_out_value);
    return true;
  }

private:
  llvm::Value* m_in_value;
  llvm::AllocaInst* m_out_value = nullptr;
};

CombinationalFilterBuilder::CombinationalFilterBuilder(WrappedLLVMContext* context, llvm::Module* mod,
                                                       const StreamGraph::FilterPermutation* filter_perm)
  : m_context(context), m_module(mod), m_filter_permutation(filter_perm),
    m_filter_decl(filter_perm->GetFilterDeclaration())
{
}

CombinationalFilterBuilder::~CombinationalFilterBuilder()
{
}

bool CombinationalFilterBuilder::GenerateCode()
{
  if (!GenerateGlobals())
    return false;

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("comb_filter_%s", m_filter_permutation->GetName().c_str());
    m_function = GenerateFunction(m_filter_decl->GetWorkBlock(), name);
    if (!m_function)
      return false;
  }
  else
  {
    m_function = nullptr;
  }

  return true;
}

llvm::Function* CombinationalFilterBuilder::GenerateFunction(AST::FilterWorkBlock* block, const std::string& name)
{
  assert(m_module->getFunction(name.c_str()) == nullptr);

  llvm::Type* ret_type;
  llvm::SmallVector<llvm::Type*, 1> params;
  if (!m_filter_permutation->GetInputType()->IsVoid())
    params.push_back(m_context->GetLLVMType(m_filter_permutation->GetInputType()));
  if (!m_filter_permutation->GetOutputType()->IsVoid())
    ret_type = m_context->GetLLVMType(m_filter_permutation->GetOutputType());
  else
    ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());

  llvm::FunctionType* func_type = llvm::FunctionType::get(ret_type, params, false);
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), func_type);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return nullptr;

  llvm::Value* in_ptr = nullptr;
  if (!m_filter_permutation->GetInputType()->IsVoid())
  {
    in_ptr = &(*func->arg_begin());
    in_ptr->setName("in_value");
  }

  // Start at the entry basic block for the work function.
  CombinationalFragmentBuilder fragment_builder(in_ptr);
  Frontend::FunctionBuilder function_builder(m_context, m_module, &fragment_builder, func);
  fragment_builder.CreateDeclarations(m_context, function_builder.GetCurrentIRBuilder(), ret_type);

  // Add global variable references
  for (const auto& it : m_filter_permutation->GetFilterParameters())
    function_builder.AddVariable(it.decl, it.value);
  for (const auto& it : m_global_variable_map)
    function_builder.AddVariable(it.first, it.second);

  // Emit code based on the work block.
  if (!block->Accept(&function_builder))
    return nullptr;

  // Final return instruction.
  if (!m_filter_permutation->GetOutputType()->IsVoid())
    function_builder.GetCurrentIRBuilder().CreateRet(
      function_builder.GetCurrentIRBuilder().CreateLoad(fragment_builder.GetOutputValue()));
  else
    function_builder.GetCurrentIRBuilder().CreateRetVoid();

  return func;
}

bool CombinationalFilterBuilder::GenerateGlobals()
{
  if (!m_filter_decl->HasStateVariables())
    return true;

  // Visit the state variable declarations, generating LLVM variables for them
  Frontend::StateVariablesBuilder gvb(m_context, m_module, m_filter_permutation->GetName());
  if (!m_filter_decl->GetStateVariables()->Accept(&gvb))
    return false;

  // And copy the table, ready to insert to the function builders
  m_global_variable_map = gvb.GetVariableMap();
  return true;
}

} // namespace HLSTarget
