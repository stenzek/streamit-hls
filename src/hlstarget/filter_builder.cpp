#include "hlstarget/filter_builder.h"
#include <cassert>
#include "common/log.h"
#include "common/string_helpers.h"
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

namespace HLSTarget
{
// Dummy interface for push/pop/peek
struct FragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
  FragmentBuilder(llvm::Value* in_ptr, llvm::Value* out_ptr) : m_in_ptr(in_ptr), m_out_ptr(out_ptr) {}

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    assert(m_in_ptr != nullptr);
    return builder.CreateLoad(m_in_ptr, true, "pop");
  }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final
  {
    Log::Warning("BuildPeek", "fixme");
    return builder.getInt32(0);
  }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    assert(m_out_ptr != nullptr);
    builder.CreateStore(value, m_out_ptr, true);
    return true;
  }

private:
  llvm::Value* m_in_ptr;
  llvm::Value* m_out_ptr;
};

FilterBuilder::FilterBuilder(WrappedLLVMContext* context, llvm::Module* mod) : m_context(context), m_module(mod)
{
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode(const StreamGraph::Filter* filter)
{
  m_filter_decl = filter->GetFilterDeclaration();
  m_instance_name = filter->GetName();
  m_output_channel_name = filter->GetOutputChannelName();

  if (!GenerateGlobals())
    return false;

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
  llvm::SmallVector<llvm::Type*, 2> params;

  if (!m_filter_decl->GetInputType()->IsVoid())
  {
    llvm::Type* llvm_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
    llvm::Type* pointer_ty = llvm::PointerType::get(llvm_ty, 0);
    assert(pointer_ty != nullptr);
    params.push_back(pointer_ty);
  }
  if (!m_filter_decl->GetOutputType()->IsVoid())
  {
    llvm::Type* llvm_ty = m_context->GetLLVMType(m_filter_decl->GetOutputType());
    llvm::Type* pointer_ty = llvm::PointerType::get(llvm_ty, 0);
    assert(pointer_ty != nullptr);
    params.push_back(pointer_ty);
  }

  llvm::FunctionType* func_type = llvm::FunctionType::get(ret_type, params, false);
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), func_type);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return nullptr;

  llvm::Value* in_ptr = nullptr;
  llvm::Value* out_ptr = nullptr;
  auto args_iter = func->arg_begin();
  if (!m_filter_decl->GetInputType()->IsVoid())
  {
    in_ptr = &(*args_iter++);
    in_ptr->setName("in_ptr");
  }
  if (!m_filter_decl->GetOutputType()->IsVoid())
  {
    out_ptr = &(*args_iter++);
    out_ptr->setName("out_ptr");
  }

  // Start at the entry basic block for the work function.
  FragmentBuilder fragment_builder(in_ptr, out_ptr);
  Frontend::FunctionBuilder entry_bb_builder(m_context, m_module, &fragment_builder, func);

  // Add global variable references
  for (const auto& it : m_global_variable_map)
    entry_bb_builder.AddGlobalVariable(it.first, it.second);

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

} // namespace HLSTarget
