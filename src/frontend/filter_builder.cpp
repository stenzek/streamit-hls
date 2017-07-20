#include "frontend/filter_builder.h"
#include <cassert>
#include "frontend/constant_expression_builder.h"
#include "frontend/context.h"
#include "frontend/filter_function_builder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "parser/helpers.h"
#include "parser/type.h"

namespace Frontend
{

FilterBuilder::FilterBuilder(Context* context, llvm::Module* module, const AST::FilterDeclaration* filter_decl)
  : m_context(context), m_module(module), m_filter_decl(filter_decl)
{
  // TODO: This should use filter instances, not filters
  m_name_prefix = m_filter_decl->GetName();
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode()
{
  if (!GenerateGlobals() || !GenerateChannelFunctions())
    return false;

  if (m_filter_decl->HasInitBlock())
  {
    std::string name = StringFromFormat("%s_init", m_name_prefix.c_str());
    m_init_function = GenerateFunction(m_filter_decl->GetInitBlock(), name);
    if (!m_init_function)
      return false;
  }

  if (m_filter_decl->HasPreworkBlock())
  {
    std::string name = StringFromFormat("%s_prework", m_name_prefix.c_str());
    m_prework_function = GenerateFunction(m_filter_decl->GetPreworkBlock(), name);
    if (!m_prework_function)
      return false;
  }

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("%s_work", m_name_prefix.c_str());
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
  FilterFunctionBuilder entry_bb_builder(this, "entry", func);

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

/// Creates LLVM global variables for each filter state variable
/// Also creates initializers where they are constant
class GlobalVariableBuilder : public AST::Visitor
{
public:
  GlobalVariableBuilder(Context* context_, const std::string& prefix_) : context(context_), prefix(prefix_) {}

  bool Visit(AST::Node* node) override
  {
    assert(0 && "Fallback handler executed");
    return false;
  }

  bool Visit(AST::VariableDeclaration* node) override
  {
    llvm::Constant* initializer = nullptr;
    if (node->HasInitializer())
    {
      // The complex initializers should have already been moved to init()
      assert(node->GetInitializer()->IsConstant());

      // The types should be the same..
      assert(node->GetInitializer()->GetType() == node->GetType());

      // Translate to a LLVM constant
      ConstantExpressionBuilder ceb(context);
      if (node->HasInitializer() && (!node->GetInitializer()->Accept(&ceb) || !ceb.IsValid()))
        return false;

      initializer = ceb.GetResultValue();
    }

    // Generate the name for it
    std::string var_name = StringFromFormat("%s_%s", prefix.c_str(), node->GetName().c_str());

    // Create LLVM global var
    llvm::Type* llvm_ty = context->GetLLVMType(node->GetType());
    llvm::GlobalVariable* llvm_var = new llvm::GlobalVariable(*context->GetModule(), llvm_ty, true,
                                                              llvm::GlobalValue::PrivateLinkage, initializer, var_name);
    global_var_map.emplace(node, llvm_var);
    return true;
  }

  Context* context;
  std::string prefix;
  std::unordered_map<const AST::VariableDeclaration*, llvm::GlobalVariable*> global_var_map;
};

bool FilterBuilder::GenerateGlobals()
{
  if (!m_filter_decl->HasStateVariables())
    return true;

  // Visit the state variable declarations, generating LLVM variables for them
  GlobalVariableBuilder gvb(m_context, m_name_prefix);
  if (!m_filter_decl->GetStateVariables()->Accept(&gvb))
    return false;

  // And copy the table, ready to insert to the function builders
  m_global_variable_map = std::move(gvb.global_var_map);
  return true;
}

bool FilterBuilder::GenerateChannelFunctions()
{
  // Pop/peek
  if (!m_filter_decl->GetInputType()->IsVoid())
  {
    llvm::Type* ret_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
    llvm::Type* llvm_peek_idx_ty = llvm::Type::getInt32Ty(m_context->GetLLVMContext());
    llvm::FunctionType* llvm_peek_fn = llvm::FunctionType::get(ret_ty, {llvm_peek_idx_ty}, false);
    llvm::FunctionType* llvm_pop_fn = llvm::FunctionType::get(ret_ty, false);
    m_peek_function = m_module->getOrInsertFunction(StringFromFormat("%s_peek", m_name_prefix.c_str()), llvm_peek_fn);
    m_pop_function = m_module->getOrInsertFunction(StringFromFormat("%s_pop", m_name_prefix.c_str()), llvm_pop_fn);
  }

  // Push
  if (!m_filter_decl->GetOutputType()->IsVoid())
  {
    llvm::Type* llvm_ty = m_context->GetLLVMType(m_filter_decl->GetOutputType());
    llvm::Type* ret_ty = llvm::Type::getVoidTy(m_context->GetLLVMContext());
    llvm::FunctionType* llvm_push_fn = llvm::FunctionType::get(ret_ty, {llvm_ty}, false);
    m_push_function = m_module->getOrInsertFunction(StringFromFormat("%s_push", m_name_prefix.c_str()), llvm_push_fn);
  }

  return true;
}

} // namespace Frontend