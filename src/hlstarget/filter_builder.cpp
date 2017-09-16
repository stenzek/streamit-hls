#include "hlstarget/filter_builder.h"
#include <cassert>
#include <memory>
#include "common/log.h"
#include "common/string_helpers.h"
#include "common/types.h"
#include "frontend/constant_expression_builder.h"
#include "frontend/function_builder.h"
#include "frontend/state_variables_builder.h"
#include "frontend/wrapped_llvm_context.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(HLSTarget::FilterBuilder);

namespace HLSTarget
{
// Buffered fragment - used for peek().
// The first time the work function runs, we pop N elements where N is peek_rate.
// This array is then indexed for peek(). When we see a pop() statement, we return the 0th element
// from this array, and move each element backwards one (1 -> 0, 2 -> 1, ...). We then pop another
// element from the input buffer for the last element in the array.
//
// An optimization here is when the pop rate equals the peek rate. In this case, we skip the move
// step, and simply return the 0th, then 1st, and so on for each pop() call, and offset any subsequent peeks.
//
struct FragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
{
  FragmentBuilder(llvm::Value* in_ptr, llvm::Value* out_ptr) : m_in_ptr(in_ptr), m_out_ptr(out_ptr) {}

  void CreateDeclarations(Frontend::FunctionBuilder* func_builder)
  {
    Frontend::WrappedLLVMContext* context = func_builder->GetContext();
    llvm::IRBuilder<>& builder = func_builder->GetCurrentIRBuilder();
    // llvm::BasicBlock* new_entry = llvm::BasicBlock::Create(context->GetLLVMContext(), "new_entry",
    // func_builder->GetFunction());
    // llvm::BasicBlock* entry_block = func_builder->GetCurrentBasicBlock();
    if (m_in_ptr)
      m_in_index = builder.CreateAlloca(context->GetIntType(), nullptr, "in_index");
    if (m_out_ptr)
      m_out_index = builder.CreateAlloca(context->GetIntType(), nullptr, "out_index");
    if (m_in_ptr)
      builder.CreateStore(builder.getInt32(0), m_in_index);
    if (m_out_ptr)
      builder.CreateStore(builder.getInt32(0), m_out_index);
    // builder.CreateBr(new_entry);
    // func_builder->SwitchBasicBlock(new_entry);
  }

  void BuildBuffer(Frontend::FunctionBuilder* func_builder, const StreamGraph::FilterPermutation* filter_perm,
                   u32 peek_rate, u32 pop_rate)
  {
    Frontend::WrappedLLVMContext* context = func_builder->GetContext();
    llvm::IRBuilder<>& builder = func_builder->GetCurrentIRBuilder();
    llvm::Function* func = func_builder->GetFunction();
    m_buffer_size = peek_rate;
    m_peek_optimization = (peek_rate == pop_rate);

    llvm::Type* buffer_element_type = filter_perm->GetInputType();
    llvm::Type* buffer_array_type = llvm::ArrayType::get(buffer_element_type, m_buffer_size);

    // Enable peek optimization for non-readahead peeks.
    if (!m_peek_optimization)
    {
      m_buffer_var =
        new llvm::GlobalVariable(*func_builder->GetModule(), buffer_array_type, false,
                                 llvm::GlobalValue::PrivateLinkage, llvm::ConstantAggregateZero::get(buffer_array_type),
                                 StringFromFormat("%s_peek_buffer", filter_perm->GetName().c_str()));

      llvm::BasicBlock* bb_fill = llvm::BasicBlock::Create(context->GetLLVMContext(), "peek_buffer_fill", func);
      llvm::BasicBlock* no_bb_fill = llvm::BasicBlock::Create(context->GetLLVMContext(), "no_peek_buffer_fill", func);

      // Create "filled" variable. This is only false for a single iteration.
      llvm::GlobalVariable* buffer_filled =
        new llvm::GlobalVariable(*func_builder->GetModule(), func_builder->GetContext()->GetBooleanType(), false,
                                 llvm::GlobalValue::PrivateLinkage, builder.getInt1(false),
                                 StringFromFormat("%s_peek_buffer_filled", filter_perm->GetName().c_str()));
      llvm::Value* comp_res = builder.CreateICmpEQ(builder.CreateLoad(buffer_filled), builder.getInt1(false));
      builder.CreateCondBr(comp_res, bb_fill, no_bb_fill);

      // Fill the peek buffer once.
      func_builder->SwitchBasicBlock(bb_fill);
      for (u32 i = 0; i < m_buffer_size; i++)
      {
        // fill_value <- pop()
        llvm::Value* fill_src_index = builder.CreateLoad(m_in_index, "fill_src_index");
        builder.CreateStore(builder.CreateAdd(fill_src_index, builder.getInt32(1)), m_in_index);
        llvm::Value* fill_value =
          builder.CreateLoad(builder.CreateGEP(m_in_ptr, {fill_src_index}, "fill_ptr"), true, "fill_value");
        // peek_buffer[i] <- fill_value
        builder.CreateStore(fill_value,
                            builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
      }
      // peek_buffer_filled <- true
      builder.CreateStore(builder.getInt1(true), buffer_filled);
      builder.CreateBr(no_bb_fill);

      // Switch to new "entry" block.
      func_builder->SwitchBasicBlock(no_bb_fill);
    }
    else
    {
      // Non-readahead peeks. Use a local variable instead of a global, since it doesn't have to be
      // maintained across work function iterations.
      m_buffer_var = builder.CreateAlloca(buffer_array_type, nullptr, "peek_buffer");

      // Fill the peek buffer.
      for (u32 i = 0; i < m_buffer_size; i++)
      {
        // fill_value <- pop()
        llvm::Value* fill_src_index = builder.CreateLoad(m_in_index, "fill_src_index");
        builder.CreateStore(builder.CreateAdd(fill_src_index, builder.getInt32(1)), m_in_index);
        llvm::Value* fill_value =
          builder.CreateLoad(builder.CreateGEP(m_in_ptr, {fill_src_index}, "fill_ptr"), true, "fill_value");
        // peek_buffer[i] <- fill_value
        builder.CreateStore(fill_value,
                            builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
      }
    }
  }

  bool IsBufferingEnabled() const { return (m_buffer_var != nullptr); }

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    assert(m_in_ptr != nullptr);

    if (!IsBufferingEnabled())
    {
      // Direct without buffering.
      llvm::Value* pop_src_index = builder.CreateLoad(m_in_index, "in_src_index");
      builder.CreateStore(builder.CreateAdd(pop_src_index, builder.getInt32(1)), m_in_index);
      return builder.CreateLoad(builder.CreateGEP(m_in_ptr, {pop_src_index}, "in_ptr"), true, "pop");
    }

    // pop_val <- peek_buffer[buffer_index]
    llvm::Value* pop_val = builder.CreateLoad(
      builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), builder.getInt32(m_buffer_index)}, "in_ptr"),
      "pop_val");

    // When using peek optimizations, increment buffer index, instead of wasting time shuffling elements around.
    if (m_peek_optimization)
    {
      m_buffer_index++;
      return pop_val;
    }

    // peek_buffer[0] = peek_buffer[1], peek_buffer[1] = peek_buffer[2], ...
    for (u32 i = 0; i < (m_buffer_size - 1); i++)
    {
      builder.CreateStore(
        builder.CreateLoad(builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), builder.getInt32(i + 1)})),
        builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
    }

    // peek_buffer[n-1] = pop()
    llvm::Value* pop_src_index = builder.CreateLoad(m_in_index, "in_src_index");
    builder.CreateStore(builder.CreateAdd(pop_src_index, builder.getInt32(1)), m_in_index);
    builder.CreateStore(
      builder.CreateLoad(builder.CreateGEP(m_in_ptr, {pop_src_index}, "pop_ptr"), true, "pop"),
      builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), builder.getInt32(m_buffer_size - 1)}));

    return pop_val;
  }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final
  {
    if (!IsBufferingEnabled())
      return nullptr;

    // peek_val <- peek_buffer[idx_value]
    llvm::Value* peek_ptr = builder.CreateInBoundsGEP(m_buffer_var, {builder.getInt32(0), idx_value}, "peek_ptr");
    return builder.CreateLoad(peek_ptr, "peek_val");
  }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    assert(m_out_ptr != nullptr);
    llvm::Value* push_dst_index = builder.CreateLoad(m_out_index, "push_dst_index");
    builder.CreateStore(builder.CreateAdd(push_dst_index, builder.getInt32(1)), m_out_index);
    builder.CreateStore(value, builder.CreateGEP(m_out_ptr, {push_dst_index}, "push_ptr"), true);
    return true;
  }

private:
  llvm::Value* m_in_ptr;
  llvm::Value* m_out_ptr;
  llvm::AllocaInst* m_in_index = nullptr;
  llvm::AllocaInst* m_out_index = nullptr;
  llvm::Value* m_buffer_var = nullptr;
  u32 m_buffer_size = 0;
  u32 m_buffer_index = 0;
  bool m_peek_optimization = false;
};

FilterBuilder::FilterBuilder(Frontend::WrappedLLVMContext* context, llvm::Module* mod,
                             const StreamGraph::FilterPermutation* filter_perm)
  : m_context(context), m_module(mod), m_filter_permutation(filter_perm),
    m_filter_decl(filter_perm->GetFilterDeclaration())
{
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode()
{
  if (!GenerateGlobals())
    return false;

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("filter_%s", m_filter_permutation->GetName().c_str());
    m_function = GenerateFunction(m_filter_decl->GetWorkBlock(), name);
    if (!m_function)
      return false;
  }
  else
  {
    m_function = nullptr;
  }

  return true;
}

llvm::Function* FilterBuilder::GenerateFunction(AST::FilterWorkBlock* block, const std::string& name)
{
  assert(m_module->getFunction(name.c_str()) == nullptr);

  llvm::Type* ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  llvm::SmallVector<llvm::Type*, 2> params;

  if (!m_filter_permutation->GetInputType()->isVoidTy())
  {
    llvm::Type* pointer_ty = llvm::PointerType::get(m_filter_permutation->GetInputType(), 0);
    assert(pointer_ty != nullptr);
    params.push_back(pointer_ty);
  }
  if (!m_filter_permutation->GetOutputType()->isVoidTy())
  {
    llvm::Type* pointer_ty = llvm::PointerType::get(m_filter_permutation->GetOutputType(), 0);
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
  if (!m_filter_permutation->GetInputType()->isVoidTy())
  {
    in_ptr = &(*args_iter++);
    in_ptr->setName("in_ptr");
  }
  if (!m_filter_permutation->GetOutputType()->isVoidTy())
  {
    out_ptr = &(*args_iter++);
    out_ptr->setName("out_ptr");
  }

  // Start at the entry basic block for the work function.
  FragmentBuilder fragment_builder(in_ptr, out_ptr);
  Frontend::FunctionBuilder function_builder(m_context, m_module, &fragment_builder, func);
  fragment_builder.CreateDeclarations(&function_builder);
  if (!m_filter_permutation->GetInputType()->isVoidTy() && m_filter_permutation->GetPeekRate() > 0)
    fragment_builder.BuildBuffer(&function_builder, m_filter_permutation, m_filter_permutation->GetPeekRate(),
                                 m_filter_permutation->GetPopRate());

  // Add global variable references
  for (const auto& it : m_filter_permutation->GetFilterParameters())
    function_builder.AddVariable(it.decl, it.value);
  for (const auto& it : m_global_variable_map)
    function_builder.AddVariable(it.first, it.second);

  // Emit code based on the work block.
  if (!block->Accept(&function_builder))
    return nullptr;

  // Final return instruction.
  function_builder.GetCurrentIRBuilder().CreateRetVoid();
  return func;
}

bool FilterBuilder::GenerateGlobals()
{
  if (!m_filter_decl->HasStateVariables())
    return true;

  // Visit the state variable declarations, generating LLVM variables for them
  Frontend::StateVariablesBuilder gvb(m_context, m_module, m_filter_permutation->GetName());
  if (!m_filter_decl->GetStateVariables()->Accept(&gvb))
    return false;

  // And copy the table, ready to insert to the function builders
  m_global_variable_map = gvb.GetVariableMap();
  return true;
}

bool FilterBuilder::CanMakeCombinational() const
{
  // Requires the filter be stateless.
  if (m_filter_decl->IsStateful() /* || m_filter_decl->HasStateVariables()*/)
    return false;

  // We require a push and pop rate of <= 1, and no peeking.
  if (m_filter_permutation->GetPopRate() > 1 || m_filter_permutation->GetPushRate() > 1 ||
      m_filter_permutation->GetPeekRate() > 0)
    return false;

  // If there is any control flow, we can't optimize it (HLS turns it into a state machine).
  // TODO: This will break with function calls.. Inline them first?
  size_t num_basic_blocks = 0;
  for (llvm::BasicBlock& BB : *m_function)
    num_basic_blocks++;
  if (num_basic_blocks > 1)
    return false;

  // TODO: The last statement should be a push.

  // Safe to make combinational.
  return true;
}

} // namespace HLSTarget
