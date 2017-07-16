#include "frontend/filter_function_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/expression_builder.h"
#include "frontend/filter_builder.h"
#include "frontend/statement_builder.h"
#include "parser/ast.h"

namespace Frontend
{

FilterFunctionBuilder::FilterFunctionBuilder(FilterBuilder* fb, const std::string& name, llvm::Function* func)
  : m_filter_builder(fb), m_name(name), m_func(func),
    m_entry_basic_block(llvm::BasicBlock::Create(fb->GetContext()->GetLLVMContext(), name, func)),
    m_current_ir_builder(m_entry_basic_block)
{
  m_current_basic_block = m_entry_basic_block;
}

FilterFunctionBuilder::~FilterFunctionBuilder()
{
}

Context* FilterFunctionBuilder::GetContext() const
{
  return m_filter_builder->GetContext();
}

FilterBuilder* FilterFunctionBuilder::GetFilterBuilder() const
{
  return m_filter_builder;
}

llvm::BasicBlock* FilterFunctionBuilder::GetEntryBasicBlock() const
{
  return m_current_basic_block;
}

llvm::BasicBlock* FilterFunctionBuilder::GetCurrentBasicBlock() const
{
  return m_current_basic_block;
}

llvm::IRBuilder<>& FilterFunctionBuilder::GetCurrentIRBuilder()
{
  return m_current_ir_builder;
}

void FilterFunctionBuilder::AddGlobalVariable(const AST::VariableDeclaration* var, llvm::GlobalVariable* gvar)
{
  assert(m_vars.find(var) == m_vars.end());
  m_vars.emplace(var, gvar);
}

llvm::AllocaInst* FilterFunctionBuilder::CreateVariable(const AST::VariableDeclaration* var)
{
  llvm::Type* ty = GetContext()->GetLLVMType(var->GetType());
  if (!ty)
    return nullptr;

  llvm::IRBuilder<> builder(m_entry_basic_block, m_entry_basic_block->begin());
  llvm::AllocaInst* ai = builder.CreateAlloca(ty);
  m_vars.emplace(var, ai);
  return ai;
}

llvm::Value* FilterFunctionBuilder::GetVariablePtr(const AST::VariableDeclaration* var)
{
  auto it = m_vars.find(var);
  if (it == m_vars.end())
  {
    assert(0 && "unresolved variable");
    return nullptr;
  }

  return it->second;
}

llvm::Value* FilterFunctionBuilder::LoadVariable(const AST::VariableDeclaration* var)
{
  auto it = m_vars.find(var);
  if (it == m_vars.end())
  {
    assert(0 && "unresolved variable");
    return nullptr;
  }

  return m_current_ir_builder.CreateLoad(it->second);
}

void FilterFunctionBuilder::StoreVariable(const AST::VariableDeclaration* var, llvm::Value* val)
{
  auto it = m_vars.find(var);
  if (it == m_vars.end())
  {
    assert(0 && "unresolved variable");
    return;
  }

  m_current_ir_builder.CreateStore(val, it->second);
}

llvm::BasicBlock* FilterFunctionBuilder::NewBasicBlock(const std::string& name)
{
  llvm::BasicBlock* old_bb = m_current_basic_block;
  SwitchBasicBlock(llvm::BasicBlock::Create(GetContext()->GetLLVMContext(), name, m_func));
  return old_bb;
}

void FilterFunctionBuilder::SwitchBasicBlock(llvm::BasicBlock* new_bb)
{
  m_current_basic_block = new_bb;
  m_current_ir_builder.SetInsertPoint(m_current_basic_block);
}

llvm::BasicBlock* FilterFunctionBuilder::GetCurrentBreakBasicBlock() const
{
  assert(!m_break_basic_block_stack.empty());
  return m_break_basic_block_stack.top();
}

void FilterFunctionBuilder::PushBreakBasicBlock(llvm::BasicBlock* bb)
{
  m_break_basic_block_stack.push(bb);
}

void FilterFunctionBuilder::PopBreakBasicBlock()
{
  assert(!m_break_basic_block_stack.empty());
  m_break_basic_block_stack.pop();
}

llvm::BasicBlock* FilterFunctionBuilder::GetCurrentContinueBasicBlock() const
{
  assert(!m_continue_basic_block_stack.empty());
  return m_continue_basic_block_stack.top();
}

void FilterFunctionBuilder::PushContinueBasicBlock(llvm::BasicBlock* bb)
{
  m_continue_basic_block_stack.push(bb);
}

void FilterFunctionBuilder::PopContinueBasicBlock()
{
  assert(!m_continue_basic_block_stack.empty());
  m_continue_basic_block_stack.pop();
}

bool FilterFunctionBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback visit method called.");
  return false;
}

bool FilterFunctionBuilder::Visit(AST::VariableDeclaration* node)
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

    StoreVariable(node, eb.GetResultValue());
  }

  return true;
}

bool FilterFunctionBuilder::Visit(AST::Statement* node)
{
  StatementBuilder stmt_builder(this);
  return node->Accept(&stmt_builder);
}
}