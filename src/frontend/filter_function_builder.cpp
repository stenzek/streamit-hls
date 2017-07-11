#include "frontend/filter_function_builder.h"
#include <cassert>
#include "frontend/context.h"
#include "frontend/expression_builder.h"
#include "frontend/statement_builder.h"
#include "parser/ast.h"

namespace Frontend
{

FilterFunctionBuilder::FilterFunctionBuilder(Context* ctx, const std::string& name, llvm::Function* func)
  : m_ctx(ctx), m_name(name), m_func(func),
    m_entry_basic_block(llvm::BasicBlock::Create(m_ctx->GetLLVMContext(), name, func)),
    m_current_ir_builder(m_entry_basic_block)
{
  m_current_basic_block = m_entry_basic_block;
}

FilterFunctionBuilder::~FilterFunctionBuilder()
{
}

Frontend::Context* FilterFunctionBuilder::GetContext() const
{
  return m_ctx;
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

llvm::AllocaInst* FilterFunctionBuilder::CreateVariable(const AST::VariableDeclaration* var)
{
  llvm::Type* ty = m_ctx->GetLLVMType(var->GetType());
  if (!ty)
    return nullptr;

  llvm::IRBuilder<> builder(m_entry_basic_block, m_entry_basic_block->begin());
  llvm::AllocaInst* ai = builder.CreateAlloca(ty);
  m_vars.emplace(var, ai);
  return ai;
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
  m_current_basic_block = llvm::BasicBlock::Create(m_ctx->GetLLVMContext(), name, m_func);
  m_current_ir_builder.SetInsertPoint(m_current_basic_block);
  return old_bb;
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