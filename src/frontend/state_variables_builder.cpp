#include "frontend/state_variables_builder.h"
#include <cassert>
#include "common/log.h"
#include "frontend/constant_expression_builder.h"
#include "frontend/function_builder.h"
#include "frontend/wrapped_llvm_context.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/TargetSelect.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(StateVariablesBuilder);

namespace Frontend
{
StateVariablesBuilder::StateVariablesBuilder(WrappedLLVMContext* context_, llvm::Module* mod_,
                                             const std::string& prefix_)
  : m_context(context_), m_module(mod_), m_prefix(prefix_)
{
}

bool StateVariablesBuilder::Visit(AST::VariableDeclaration* node)
{
  llvm::Type* llvm_ty = m_context->GetLLVMType(node->GetType());

  llvm::Constant* initializer = nullptr;
  if (node->HasInitializer())
  {
    // The types should be the same..
    assert(*node->GetInitializer()->GetType() == *node->GetType());

    // Translate to a LLVM constant
    ConstantExpressionBuilder ceb(m_context);
    if (node->HasInitializer() && (!node->GetInitializer()->Accept(&ceb) || !ceb.IsValid()))
      return false;

    initializer = ceb.GetResultValue();
  }
  else
  {
    // Default to zero, we need an initializer otherwise it makes it external..
    if (llvm_ty->isArrayTy() || llvm_ty->isStructTy())
      initializer = llvm::ConstantAggregateZero::get(llvm_ty);
    else
      initializer = llvm::Constant::getNullValue(llvm_ty);
  }

  // Generate the name for it
  std::string var_name = StringFromFormat("%s_%s", m_prefix.c_str(), node->GetName().c_str());

  // Create LLVM global var
  llvm::GlobalVariable* llvm_var =
    new llvm::GlobalVariable(*m_module, llvm_ty, false, llvm::GlobalValue::PrivateLinkage, initializer, var_name);

  m_global_var_map.emplace(node, llvm_var);
  return true;
}

bool StateVariablesBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback handler executed");
  return false;
}

bool StateVariablesBuilder::EvaluateInitBlock(const StreamGraph::FilterPermutation* filter_perm)
{
  std::string name = StringFromFormat("evalinit_init_%s", filter_perm->GetName().c_str());
  auto mod = std::unique_ptr<llvm::Module>(m_context->CreateModule(name.c_str()));

  StateVariablesBuilder gvb(m_context, mod.get(), filter_perm->GetName());
  if (filter_perm->GetFilterDeclaration()->HasStateVariables() &&
      !filter_perm->GetFilterDeclaration()->GetStateVariables()->Accept(&gvb))
  {
    Log_ErrorPrintf("Failed to generate state variables for init block %s", filter_perm->GetName().c_str());
    return false;
  }

  llvm::Function* func = CompileInitBlock(filter_perm, mod.get(), gvb.m_global_var_map);
  if (!func)
  {
    Log_ErrorPrintf("Failed to compile init block %s", filter_perm->GetName().c_str());
    return false;
  }

  auto ee = ExecuteInitBlock(std::move(mod), func);
  if (!ee)
    return false;

  return ReadbackInitBlock(gvb.m_global_var_map, ee.get(), filter_perm->GetFilterDeclaration()->IsStateless());
}

class InitFragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
public:
  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override { return nullptr; }
  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override { return nullptr; }
  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override { return false; }
};

llvm::Function* StateVariablesBuilder::CompileInitBlock(const StreamGraph::FilterPermutation* filter_perm,
                                                        llvm::Module* mod, VariableMap& gvm)
{
  std::string func_name = StringFromFormat("%s_init", filter_perm->GetName().c_str());
  llvm::Function* func =
    llvm::dyn_cast<llvm::Function>(mod->getOrInsertFunction(func_name, m_context->GetVoidType(), nullptr));

  InitFragmentBuilder init_fragment_builder;
  Frontend::FunctionBuilder function_builder(m_context, mod, &init_fragment_builder, func);

  // Add global variable references
  for (const auto& it : filter_perm->GetFilterParameters())
    function_builder.AddVariable(it.decl, it.value);
  for (const auto& it : gvm)
  {
    // We can't use private linkage otherwise it's not visible when we get the symbol address.
    it.second->setConstant(false);
    it.second->setLinkage(llvm::GlobalValue::CommonLinkage);
    function_builder.AddVariable(it.first, it.second);
  }

  // Generate a function based on the statements in the init block.
  if (!filter_perm->GetFilterDeclaration()->GetInitBlock()->Accept(&function_builder))
  {
    Log_ErrorPrintf("IRgen failed for init block");
    return nullptr;
  }

  function_builder.GetCurrentIRBuilder().CreateRetVoid();
  return func;
}

std::unique_ptr<llvm::ExecutionEngine> StateVariablesBuilder::ExecuteInitBlock(std::unique_ptr<llvm::Module> mod,
                                                                               llvm::Function* func)
{
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  // m_context->DumpModule(mod.get());
  if (!m_context->VerifyModule(mod.get()))
  {
    Log_ErrorPrintf("Init module failed verification");
    m_context->DumpModule(mod.get());
    return nullptr;
  }

  std::string error_msg;
  auto execution_engine =
    std::unique_ptr<llvm::ExecutionEngine>(llvm::EngineBuilder(std::move(mod)).setErrorStr(&error_msg).create());
  if (!execution_engine)
  {
    Log_ErrorPrintf("Failed to create LLVM execution engine: %s", error_msg.c_str());
    return nullptr;
  }

  execution_engine->finalizeObject();

  uint64_t func_address = execution_engine->getFunctionAddress(func->getName());
  if (!func_address)
  {
    Log_ErrorPrintf("Could not get function address for %s", func->getName().str().c_str());
    return nullptr;
  }

  Log_DevPrintf("Executing %s...", func->getName().str().c_str());
  auto func_ptr = reinterpret_cast<void (*)()>(func_address);
  func_ptr();

  return execution_engine;
}

bool StateVariablesBuilder::ReadbackInitBlock(const VariableMap& gvm, llvm::ExecutionEngine* execution_engine,
                                              bool stateless)
{
  for (const auto& it : gvm)
  {
    llvm::GlobalVariable* var = it.second;
    uint64_t value_addr = execution_engine->getGlobalValueAddress(var->getName());
    if (!value_addr)
    {
      Log_ErrorPrintf("Could not get pointer to init jit global %s", it.first->GetName().c_str());
      return false;
    }

    const void* value_ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(value_addr));
    llvm::Constant* cons = m_context->CreateConstantFromPointer(var->getType(), value_ptr);
    if (!cons)
    {
      Log_ErrorPrintf("Could not get LLVM constant for jit global %s", it.first->GetName().c_str());
      return false;
    }

    auto base_it = m_global_var_map.find(it.first);
    assert(base_it != m_global_var_map.end());
    base_it->second->setInitializer(cons);

    // Stateless filters can't modify filter variables.
    base_it->second->setConstant(stateless);
  }

  return true;
}
}