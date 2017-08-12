#include "hlstarget/filter_builder.h"
#include <cassert>
#include "common/log.h"
#include "common/string_helpers.h"
#include "core/type.h"
#include "core/wrapped_llvm_context.h"
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

namespace HLSTarget
{
// Dummy interface for push/pop/peek
struct FragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
  FragmentBuilder() {}

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    Log::Warning("BuildPop", "fixme");
    return builder.getInt32(0);
  }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final
  {
    Log::Warning("BuildPeek", "fixme");
    return builder.getInt32(0);
  }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    Log::Warning("BuildPush", "fixme");
    return true;
  }

private:
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
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), ret_type, nullptr);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return nullptr;

  // Start at the entry basic block for the work function.
  FragmentBuilder fragment_builder;
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
