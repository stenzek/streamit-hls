#include "frontend/statement_builder.h"
#include <cassert>
#include "frontend/expression_builder.h"
#include "frontend/function_builder.h"
#include "frontend/wrapped_llvm_context.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"

namespace Frontend
{
StatementBuilder::StatementBuilder(FunctionBuilder* bb_builder) : m_func_builder(bb_builder)
{
}

StatementBuilder::~StatementBuilder()
{
}

WrappedLLVMContext* StatementBuilder::GetContext() const
{
  return m_func_builder->GetContext();
}

llvm::Module* StatementBuilder::GetModule() const
{
  return m_func_builder->GetModule();
}

llvm::IRBuilder<>& StatementBuilder::GetIRBuilder() const
{
  return m_func_builder->GetCurrentIRBuilder();
}

bool StatementBuilder::Visit(AST::Node* node)
{
  assert(0 && "Fallback handler executed");
  return false;
}

bool StatementBuilder::Visit(AST::ExpressionStatement* node)
{
  // No need to assign the result to anywhere.
  ExpressionBuilder eb(m_func_builder);
  if (!node->GetInnerExpression()->Accept(&eb) || !eb.IsValid())
    return false;

  return true;
}

bool StatementBuilder::Visit(AST::IfStatement* node)
{
  ExpressionBuilder eb(m_func_builder);
  if (!node->GetInnerExpression()->Accept(&eb) || !eb.IsValid())
    return false;

  // Expression needs to be boolean
  if (!node->GetInnerExpression()->GetType()->IsBoolean())
    return false;

  // Create a basic block for the then part
  auto* before_branch_bb = m_func_builder->NewBasicBlock();

  // Evaluate then statements into this new block
  if (!node->GetThenStatements()->Accept(m_func_builder))
    return false;

  // Create a new basic block, either for the else part, or the next statements
  auto* then_bb = m_func_builder->NewBasicBlock();
  auto* else_bb = m_func_builder->GetCurrentBasicBlock();
  auto* merge_bb = m_func_builder->GetCurrentBasicBlock();

  // If there is an else part, place them in this block
  if (node->HasElseStatements())
  {
    if (!node->GetElseStatements()->Accept(m_func_builder))
      return false;

    // Jump to the merge bb after the else
    else_bb = m_func_builder->NewBasicBlock();
    merge_bb = m_func_builder->GetCurrentBasicBlock();
    llvm::IRBuilder<>(else_bb).CreateBr(merge_bb);
  }

  // Jump to the then bb, otherwise the else bb
  llvm::IRBuilder<>(before_branch_bb).CreateCondBr(eb.GetResultValue(), then_bb, else_bb);

  // Jump to the merge bb at the end of the then bb
  llvm::IRBuilder<>(then_bb).CreateBr(merge_bb);
  return true;
}

bool StatementBuilder::Visit(AST::ForStatement* node)
{
  // We always execute the init statements
  if (node->HasInitStatements() && !node->GetInitStatements()->Accept(m_func_builder))
    return false;

  // Assemble the basic block for the condition, and the inner statements
  auto* before_for_bb = m_func_builder->NewBasicBlock();
  auto* continue_bb = m_func_builder->NewBasicBlock();
  auto* condition_bb = m_func_builder->NewBasicBlock();
  auto* inner_bb = m_func_builder->NewBasicBlock();
  auto* break_bb = m_func_builder->GetCurrentBasicBlock();

  // Jump to the condition basic block immediately
  m_func_builder->SwitchBasicBlock(before_for_bb);
  GetIRBuilder().CreateBr(condition_bb);

  // Set up scope
  m_func_builder->PushContinueBasicBlock(continue_bb);
  m_func_builder->PushBreakBasicBlock(break_bb);

  // Build the condition basic block
  m_func_builder->SwitchBasicBlock(condition_bb);
  if (node->HasConditionExpression())
  {
    ExpressionBuilder eb(m_func_builder);
    assert(node->GetConditionExpression()->GetType()->IsBoolean());
    if (!node->GetConditionExpression()->Accept(&eb) || !eb.IsValid())
      return false;

    // Jump if true to the inner block, otherwise out
    GetIRBuilder().CreateCondBr(eb.GetResultValue(), inner_bb, break_bb);
  }
  else
  {
    // Always jump to the inner block
    GetIRBuilder().CreateBr(inner_bb);
  }

  // Build the continue block (runs the increment statement, then checks condition)
  m_func_builder->SwitchBasicBlock(continue_bb);
  if (node->HasLoopExpression())
  {
    ExpressionBuilder eb(m_func_builder);
    if (!node->GetLoopExpression()->Accept(&eb) || !eb.IsValid())
      return false;
  }
  GetIRBuilder().CreateBr(condition_bb);

  // Build the inner statement block
  m_func_builder->SwitchBasicBlock(inner_bb);
  if (node->HasInnerStatements() && !node->GetInnerStatements()->Accept(m_func_builder))
    return false;
  // After the inner statements, jump to the continue block implicitly
  GetIRBuilder().CreateBr(continue_bb);

  // Restore state back to the end
  m_func_builder->SwitchBasicBlock(break_bb);
  return true;
}

bool StatementBuilder::Visit(AST::BreakStatement* node)
{
  llvm::BasicBlock* break_block = m_func_builder->GetCurrentBreakBasicBlock();
  assert(break_block);
  GetIRBuilder().CreateBr(break_block);
  return true;
}

bool StatementBuilder::Visit(AST::ContinueStatement* node)
{
  llvm::BasicBlock* continue_block = m_func_builder->GetCurrentContinueBasicBlock();
  assert(continue_block);
  GetIRBuilder().CreateBr(continue_block);
  return true;
}

bool StatementBuilder::Visit(AST::ReturnStatement* node)
{
  assert(!node->HasReturnValue());
  GetIRBuilder().CreateRetVoid();
  return true;
}

bool StatementBuilder::Visit(AST::PushStatement* node)
{
  ExpressionBuilder eb(m_func_builder);
  if (!node->GetValueExpression()->Accept(&eb) || !eb.IsValid())
    return false;

  return m_func_builder->GetTargetFragmentBuilder()->BuildPush(GetIRBuilder(), eb.GetResultValue());
}

bool StatementBuilder::Visit(AST::AddStatement* node)
{
  // look up stream function with correct type signature based on args
  // if this fails, it means they don't match and we stuffed up somewhere
  std::string func_name = StringFromFormat("%s_add", node->GetStreamName().c_str());
  llvm::Function* func = GetModule()->getFunction(func_name);
  assert(func && "referenced filter exists");

  std::vector<llvm::Value*> func_params;
  for (AST::Node* param_node : *node->GetStreamParameters())
  {
    ExpressionBuilder param_eb(m_func_builder);
    if (!param_node->Accept(&param_eb) || !param_eb.IsValid())
      return false;

    func_params.push_back(param_eb.GetResultValue());
  }

  GetIRBuilder().CreateCall(func, func_params);
  return true;
}

bool StatementBuilder::Visit(AST::SplitStatement* node)
{
  llvm::Function* func = GetModule()->getFunction("StreamGraphBuilder_Split");
  assert(func && "Split function exists");

  // Mode
  std::vector<llvm::Value*> func_params;
  func_params.push_back(GetIRBuilder().getInt32((node->GetType() == AST::SplitStatement::Duplicate) ? 0 : 1));

  if (node->GetDistribution() != nullptr)
  {
    // Distribution - number of args
    func_params.push_back(GetIRBuilder().getInt32(static_cast<int>(node->GetDistribution()->GetNumChildren())));
    for (AST::Node* child_node : *node->GetDistribution())
    {
      ExpressionBuilder param_eb(m_func_builder);
      if (!child_node->Accept(&param_eb) || !param_eb.IsValid())
        return false;

      func_params.push_back(param_eb.GetResultValue());
    }
  }
  else
  {
    // No distribution
    func_params.push_back(GetIRBuilder().getInt32(0));
  }

  GetIRBuilder().CreateCall(func, func_params);
  return true;
}

bool StatementBuilder::Visit(AST::JoinStatement* node)
{
  llvm::Function* func = GetModule()->getFunction("StreamGraphBuilder_Join");
  assert(func && "Join function exists");

  std::vector<llvm::Value*> func_params;
  if (node->GetDistribution() != nullptr)
  {
    // Distribution - number of args
    func_params.push_back(GetIRBuilder().getInt32(static_cast<int>(node->GetDistribution()->GetNumChildren())));
    for (AST::Node* child_node : *node->GetDistribution())
    {
      ExpressionBuilder param_eb(m_func_builder);
      if (!child_node->Accept(&param_eb) || !param_eb.IsValid())
        return false;

      func_params.push_back(param_eb.GetResultValue());
    }
  }
  else
  {
    // No distribution
    func_params.push_back(GetIRBuilder().getInt32(0));
  }

  GetIRBuilder().CreateCall(func, func_params);
  return true;
}
}