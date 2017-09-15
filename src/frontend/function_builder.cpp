#include "frontend/function_builder.h"
#include <cassert>
#include "frontend/expression_builder.h"
#include "frontend/statement_builder.h"
#include "frontend/wrapped_llvm_context.h"
#include "parser/ast.h"

namespace Frontend
{

FunctionBuilder::FunctionBuilder(WrappedLLVMContext* ctx, llvm::Module* mod, TargetFragmentBuilder* target_builder,
                                 llvm::Function* func)
  : m_context(ctx), m_module(mod), m_target_builder(target_builder), m_func(func),
    m_entry_basic_block(llvm::BasicBlock::Create(ctx->GetLLVMContext(), "entry", func)),
    m_current_ir_builder(m_entry_basic_block)
{
  m_current_basic_block = m_entry_basic_block;
}

FunctionBuilder::~FunctionBuilder()
{
}

void FunctionBuilder::CreateParameterVariables(const std::vector<AST::ParameterDeclaration*>* func_params)
{
  auto arg_cur = m_func->arg_begin();
  auto arg_end = m_func->arg_end();

  for (const AST::ParameterDeclaration* param_decl : *func_params)
  {
    assert(arg_cur != arg_end && "out of function arguments");
    llvm::Type* decl_type = m_context->GetLLVMType(param_decl->GetType());
    llvm::Value* arg_value = &(*arg_cur++);
    assert(decl_type == arg_value->getType());

    // TODO: We would need to alloca a local copy if we allow modifying parameters/captured variables.
    arg_value->setName(param_decl->GetName());
    m_vars.emplace(param_decl, arg_value);
  }
}

void FunctionBuilder::AddVariable(const AST::Declaration* var, llvm::Value* val)
{
  assert(m_vars.find(var) == m_vars.end());
  m_vars.emplace(var, val);
}

llvm::AllocaInst* FunctionBuilder::CreateVariable(const AST::Declaration* var)
{
  llvm::Type* ty = GetContext()->GetLLVMType(var->GetType());
  if (!ty)
    return nullptr;

  llvm::IRBuilder<> builder(m_entry_basic_block, m_entry_basic_block->begin());
  llvm::AllocaInst* ai = builder.CreateAlloca(ty);
  m_vars.emplace(var, ai);
  return ai;
}

llvm::Value* FunctionBuilder::GetVariable(const AST::Declaration* var)
{
  auto it = m_vars.find(var);
  if (it == m_vars.end())
  {
    assert(0 && "unresolved variable");
    return nullptr;
  }

  return it->second;
}

llvm::BasicBlock* FunctionBuilder::NewBasicBlock(const std::string& name)
{
  llvm::BasicBlock* old_bb = m_current_basic_block;
  SwitchBasicBlock(llvm::BasicBlock::Create(GetContext()->GetLLVMContext(), name, m_func));
  return old_bb;
}

void FunctionBuilder::SwitchBasicBlock(llvm::BasicBlock* new_bb)
{
  m_current_basic_block = new_bb;
  m_current_ir_builder.SetInsertPoint(m_current_basic_block);
}

llvm::BasicBlock* FunctionBuilder::GetCurrentBreakBasicBlock() const
{
  assert(!m_break_basic_block_stack.empty());
  return m_break_basic_block_stack.top();
}

void FunctionBuilder::PushBreakBasicBlock(llvm::BasicBlock* bb)
{
  m_break_basic_block_stack.push(bb);
}

void FunctionBuilder::PopBreakBasicBlock()
{
  assert(!m_break_basic_block_stack.empty());
  m_break_basic_block_stack.pop();
}

llvm::BasicBlock* FunctionBuilder::GetCurrentContinueBasicBlock() const
{
  assert(!m_continue_basic_block_stack.empty());
  return m_continue_basic_block_stack.top();
}

void FunctionBuilder::PushContinueBasicBlock(llvm::BasicBlock* bb)
{
  m_continue_basic_block_stack.push(bb);
}

void FunctionBuilder::PopContinueBasicBlock()
{
  assert(!m_continue_basic_block_stack.empty());
  m_continue_basic_block_stack.pop();
}

bool FunctionBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback visit method called.");
  return false;
}

bool FunctionBuilder::Visit(AST::VariableDeclaration* node)
{
  auto var = CreateVariable(node);
  if (!var)
  {
    assert(0 && "failed creating variable");
    return false;
  }

  // Does the variable have an initializer?
  if (node->HasInitializer())
  {
    // We should insert the initializer in the block it's defined, not in the root.
    ExpressionBuilder eb(this);
    if (!node->GetInitializer()->Accept(&eb) || !eb.IsValid())
    {
      assert(0 && "failed creating variable initializer");
      return false;
    }

    m_current_ir_builder.CreateStore(eb.GetResultValue(), GetVariable(node));
  }

  return true;
}

bool FunctionBuilder::Visit(AST::Statement* node)
{
  StatementBuilder stmt_builder(this);
  return node->Accept(&stmt_builder);
}

llvm::FunctionType* FunctionBuilder::GetFunctionType(WrappedLLVMContext* context,
                                                     const std::vector<AST::ParameterDeclaration*>* func_params)
{
  llvm::Type* ret_type = context->GetVoidType();
  std::vector<llvm::Type*> param_types;
  for (const AST::ParameterDeclaration* param_decl : *func_params)
    param_types.push_back(context->GetLLVMType(param_decl->GetType()));

  return llvm::FunctionType::get(ret_type, param_types, false);
}
}