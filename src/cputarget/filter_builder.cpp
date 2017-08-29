#include "cputarget/filter_builder.h"
#include <cassert>
#include "common/string_helpers.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
#include "cputarget/debug_print_builder.h"
#include "frontend/constant_expression_builder.h"
#include "frontend/function_builder.h"
#include "frontend/state_variables_builder.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"

namespace CPUTarget
{
// Dummy interface for push/pop/peek
struct FragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
  FragmentBuilder(WrappedLLVMContext* context, const std::string& filter_name, llvm::Constant* peek_function,
                  llvm::Constant* pop_function, llvm::Constant* push_function)
    : m_context(context), m_filter_name(filter_name), m_peek_function(peek_function), m_pop_function(pop_function),
      m_push_function(push_function)
  {
  }

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    if (!m_pop_function)
    {
      // Not valid, should have been caught at semantic analysis time.
      return nullptr;
    }

    return builder.CreateCall(m_pop_function);
  }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final
  {
    if (!m_peek_function)
    {
      // Not valid, should have been caught at semantic analysis time.
      return nullptr;
    }

    return builder.CreateCall(m_peek_function, {idx_value});
  }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    if (!m_push_function)
    {
      // Not valid, should have been caught at semantic analysis time.
      return false;
    }

    BuildDebugPrintf(m_context, builder, StringFromFormat("%s push %%d", m_filter_name.c_str()).c_str(), {value});
    builder.CreateCall(m_push_function, {value});
    return true;
  }

private:
  WrappedLLVMContext* m_context;
  std::string m_filter_name;
  llvm::Constant* m_peek_function;
  llvm::Constant* m_pop_function;
  llvm::Constant* m_push_function;
};

FilterBuilder::FilterBuilder(WrappedLLVMContext* context, llvm::Module* mod) : m_context(context), m_module(mod)
{
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode(const StreamGraph::Filter* filter)
{
  m_filter_permutation = filter->GetFilterPermutation();
  m_filter_decl = m_filter_permutation->GetFilterDeclaration();
  m_instance_name = filter->GetName();
  m_output_channel_name = filter->GetOutputChannelName();

  if (!GenerateGlobals() || !GenerateChannelPrototypes())
    return false;

  if (m_filter_decl->HasInitBlock())
  {
    std::string name = StringFromFormat("%s_init", m_instance_name.c_str());
    m_init_function = GenerateFunction(m_filter_decl->GetInitBlock(), name);
    if (!m_init_function)
      return false;
  }
  else
  {
    m_init_function = nullptr;
  }

  if (m_filter_decl->HasPreworkBlock())
  {
    std::string name = StringFromFormat("%s_prework", m_instance_name.c_str());
    m_prework_function = GenerateFunction(m_filter_decl->GetPreworkBlock(), name);
    if (!m_prework_function)
      return false;
  }
  else
  {
    m_prework_function = nullptr;
  }

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("%s_work", m_instance_name.c_str());
    m_work_function = GenerateFunction(m_filter_decl->GetWorkBlock(), name);
    if (!m_work_function)
      return false;
  }
  else
  {
    m_work_function = nullptr;
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

  // All our filter functions should be private/static. This way they can be inlined.
  func->setLinkage(llvm::GlobalValue::PrivateLinkage);

  // Start at the entry basic block for the work function.
  FragmentBuilder fragment_builder(m_context, name, m_peek_function, m_pop_function, m_push_function);
  Frontend::FunctionBuilder entry_bb_builder(m_context, m_module, &fragment_builder, func);

  // Add constants for the filter parameters
  for (const auto& it : m_filter_permutation->GetFilterParameters())
    entry_bb_builder.AddVariable(it.decl, it.value);

  // Add global variable references
  for (const auto& it : m_global_variable_map)
    entry_bb_builder.AddVariable(it.first, it.second);

  // Emit code based on the work block.
  if (!block->Accept(&entry_bb_builder))
    return nullptr;

  // Final return instruction.
  entry_bb_builder.GetCurrentIRBuilder().CreateRetVoid();
  return func;
}

bool FilterBuilder::GenerateGlobals()
{
  if (!m_filter_decl->HasStateVariables())
    return true;

  // Visit the state variable declarations, generating LLVM variables for them
  Frontend::StateVariablesBuilder gvb(m_context, m_module, m_instance_name);
  if (!m_filter_decl->GetStateVariables()->Accept(&gvb))
    return false;

  // And copy the table, ready to insert to the function builders
  m_global_variable_map = gvb.GetVariableMap();
  return true;
}

bool FilterBuilder::GenerateChannelPrototypes()
{
  // TODO: Don't generate these prototypes when the rate is zero?
  if (!m_filter_permutation->GetInputType()->IsVoid())
  {
    // Peek
    llvm::Type* ret_ty = m_context->GetLLVMType(m_filter_permutation->GetInputType());
    llvm::FunctionType* llvm_peek_fn = llvm::FunctionType::get(ret_ty, {m_context->GetIntType()}, false);
    m_peek_function = m_module->getOrInsertFunction(StringFromFormat("%s_peek", m_instance_name.c_str()), llvm_peek_fn);
    if (!m_peek_function)
      return false;

    // Pop
    llvm::FunctionType* llvm_pop_fn = llvm::FunctionType::get(ret_ty, false);
    m_pop_function = m_module->getOrInsertFunction(StringFromFormat("%s_pop", m_instance_name.c_str()), llvm_pop_fn);
    if (!m_pop_function)
      return false;
  }

  if (!m_filter_permutation->GetOutputType()->IsVoid())
  {
    // Push - this needs the name of the output filter
    llvm::Type* param_ty = m_context->GetLLVMType(m_filter_permutation->GetOutputType());
    llvm::FunctionType* llvm_push_fn = llvm::FunctionType::get(m_context->GetVoidType(), {param_ty}, false);
    m_push_function =
      m_module->getOrInsertFunction(StringFromFormat("%s_push", m_output_channel_name.c_str()), llvm_push_fn);
    if (!m_push_function)
      return false;
  }

  return true;
}

} // namespace CPUTarget
