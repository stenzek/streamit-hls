#include "frontend/filter_builder.h"
#include <cassert>
#include "common/string_helpers.h"
#include "frontend/constant_expression_builder.h"
#include "frontend/context.h"
#include "frontend/filter_function_builder.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "parser/type.h"

namespace Frontend
{

FilterBuilder::FilterBuilder(Context* context, llvm::Module* mod, const AST::FilterDeclaration* filter_decl,
                             const std::string& instance_name, const std::string& output_instance_name)
  : m_context(context), m_module(mod), m_filter_decl(filter_decl), m_instance_name(instance_name),
    m_output_channel_name(output_instance_name)
{
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode()
{
  if (!GenerateGlobals() || !GenerateChannelPrototypes())
    return false;

  if (m_filter_decl->HasInitBlock())
  {
    std::string name = StringFromFormat("%s_init", m_instance_name.c_str());
    m_init_function = GenerateFunction(m_filter_decl->GetInitBlock(), name);
    if (!m_init_function)
      return false;
  }

  if (m_filter_decl->HasPreworkBlock())
  {
    std::string name = StringFromFormat("%s_prework", m_instance_name.c_str());
    m_prework_function = GenerateFunction(m_filter_decl->GetPreworkBlock(), name);
    if (!m_prework_function)
      return false;
  }

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("%s_work", m_instance_name.c_str());
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
  FilterFunctionBuilder entry_bb_builder(m_context, m_module, this, "entry", func);

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
  GlobalVariableBuilder(Context* context_, llvm::Module* mod_, const std::string& prefix_)
    : context(context_), mod(mod_), prefix(prefix_)
  {
  }

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
    llvm::GlobalVariable* llvm_var =
      new llvm::GlobalVariable(*mod, llvm_ty, true, llvm::GlobalValue::PrivateLinkage, initializer, var_name);

    if (initializer)
      llvm_var->setConstant(false);

    global_var_map.emplace(node, llvm_var);
    return true;
  }

  Context* context;
  llvm::Module* mod;
  std::string prefix;
  std::unordered_map<const AST::VariableDeclaration*, llvm::GlobalVariable*> global_var_map;
};

bool FilterBuilder::GenerateGlobals()
{
  if (!m_filter_decl->HasStateVariables())
    return true;

  // Visit the state variable declarations, generating LLVM variables for them
  GlobalVariableBuilder gvb(m_context, m_module, m_instance_name);
  if (!m_filter_decl->GetStateVariables()->Accept(&gvb))
    return false;

  // And copy the table, ready to insert to the function builders
  m_global_variable_map = std::move(gvb.global_var_map);
  return true;
}

bool FilterBuilder::GenerateChannelPrototypes()
{
  // TODO: Don't generate these prototypes when the rate is zero?
  if (!m_filter_decl->GetInputType()->IsVoid())
  {
    // Peek
    llvm::Type* ret_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
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

  if (!m_filter_decl->GetOutputType()->IsVoid())
  {
    // Push - this needs the name of the output filter
    llvm::Type* param_ty = m_context->GetLLVMType(m_filter_decl->GetOutputType());
    llvm::FunctionType* llvm_push_fn = llvm::FunctionType::get(m_context->GetVoidType(), {param_ty}, false);
    m_push_function =
      m_module->getOrInsertFunction(StringFromFormat("%s_push", m_output_channel_name.c_str()), llvm_push_fn);
    if (!m_push_function)
      return false;
  }

  return true;
}

} // namespace Frontend
