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
#include "llvm/IR/Attributes.h"
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
  bool IsInputBufferingEnabled() const { return (m_input_buffer_var != nullptr); }
  bool IsOutputBufferingEnabled() const { return (m_output_buffer_var != nullptr); }

  llvm::Value* ReadFromChannel(llvm::IRBuilder<>& builder, u32 channel)
  {
    return builder.CreateLoad(m_in_ptrs[channel], true, "pop_value");
  }

  void WriteToChannel(llvm::IRBuilder<>& builder, u32 channel, llvm::Value* value)
  {
    builder.CreateStore(value, m_out_ptrs[channel], true);
  }

  void BuildPrologue(Frontend::FunctionBuilder* func_builder, const StreamGraph::FilterPermutation* filter_perm)
  {
    Frontend::WrappedLLVMContext* context = func_builder->GetContext();
    llvm::IRBuilder<>& builder = func_builder->GetCurrentIRBuilder();
    llvm::Function* func = func_builder->GetFunction();

    // Set the "filter" attribute on the function itself.
    func->addFnAttr(llvm::Attribute::get(context->GetLLVMContext(), "streamit_filter"));

    // Retreive arguments, set names and attributes.
    auto args_iter = func->arg_begin();
    u32 arg_index = 0;
    if (!filter_perm->GetInputType()->isVoidTy())
    {
      for (u32 i = 0; i < filter_perm->GetInputChannelWidth(); i++)
      {
        llvm::Value* arg = &(*args_iter++);
        arg->setName(StringFromFormat("in_channel_%u", i));
        func->addAttribute(arg_index + 1, llvm::Attribute::get(context->GetLLVMContext(), "streamit_fifo"));
        m_in_ptrs.push_back(arg);
        arg_index++;
      }

      // We need to use buffering if we have a wide channel, or peeking.
      if (!filter_perm->GetInputChannelWidth() > 0 || filter_perm->GetPeekRate() > 0)
        BuildInputBuffer(func_builder, filter_perm);
    }
    if (!filter_perm->GetOutputType()->isVoidTy())
    {
      for (u32 i = 0; i < filter_perm->GetOutputChannelWidth(); i++)
      {
        llvm::Value* arg = &(*args_iter++);
        arg->setName(StringFromFormat("out_channel_%u", i));
        func->addAttribute(arg_index + 1, llvm::Attribute::get(context->GetLLVMContext(), "streamit_fifo"));
        m_out_ptrs.push_back(arg);
        arg_index++;
      }

      // We need to use buffering if we have a wide channel.
      if (filter_perm->GetOutputChannelWidth() > 0)
        BuildOutputBuffer(func_builder, filter_perm);
    }
  }

  void BuildInputBuffer(Frontend::FunctionBuilder* func_builder, const StreamGraph::FilterPermutation* filter_perm)
  {
    Frontend::WrappedLLVMContext* context = func_builder->GetContext();
    llvm::IRBuilder<>& builder = func_builder->GetCurrentIRBuilder();

    m_input_buffer_size = (filter_perm->GetPeekRate() > 0) ? filter_perm->GetPeekRate() : filter_perm->GetPopRate();
    m_peek_optimization = (filter_perm->GetPeekRate() <= filter_perm->GetPopRate());
    Log_DevPrintf("%s peek optimization for this filter", m_peek_optimization ? "using" : "not using");

    llvm::Type* buffer_element_type = filter_perm->GetInputType();
    llvm::Type* buffer_array_type = llvm::ArrayType::get(buffer_element_type, m_input_buffer_size);

    // Enable peek optimization for non-readahead peeks.
    if (!m_peek_optimization)
    {
      m_input_buffer_var =
        new llvm::GlobalVariable(*func_builder->GetModule(), buffer_array_type, false,
                                 llvm::GlobalValue::PrivateLinkage, llvm::ConstantAggregateZero::get(buffer_array_type),
                                 StringFromFormat("%s_peek_buffer", filter_perm->GetName().c_str()));

      llvm::BasicBlock* bb_fill =
        llvm::BasicBlock::Create(context->GetLLVMContext(), "peek_buffer_fill", func_builder->GetFunction());
      llvm::BasicBlock* no_bb_fill =
        llvm::BasicBlock::Create(context->GetLLVMContext(), "no_peek_buffer_fill", func_builder->GetFunction());
      llvm::BasicBlock* after_bb_fill =
        llvm::BasicBlock::Create(context->GetLLVMContext(), "after_peek_buffer_fill", func_builder->GetFunction());

      // Create "filled" variable. This is only false for a single iteration.
      llvm::GlobalVariable* buffer_filled =
        new llvm::GlobalVariable(*func_builder->GetModule(), func_builder->GetContext()->GetBooleanType(), false,
                                 llvm::GlobalValue::PrivateLinkage, builder.getInt1(false),
                                 StringFromFormat("%s_peek_buffer_filled", filter_perm->GetName().c_str()));
      llvm::Value* comp_res = builder.CreateICmpEQ(builder.CreateLoad(buffer_filled), builder.getInt1(false));
      builder.CreateCondBr(comp_res, bb_fill, no_bb_fill);

      // Fill the peek buffer once.
      func_builder->SwitchBasicBlock(bb_fill);
      for (u32 i = 0; i < m_input_buffer_size; i++)
      {
        // peek_buffer[i] <- pop()
        builder.CreateStore(ReadFromChannel(builder, 0),
                            builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
      }
      // peek_buffer_filled <- true
      builder.CreateStore(builder.getInt1(true), buffer_filled);
      builder.CreateBr(after_bb_fill);

      // Fill the peek buffer as many times is needed.
      // If we wanted this with multi-channel, we'd need a state machine..
      func_builder->SwitchBasicBlock(no_bb_fill);
      for (u32 i = 0; i < filter_perm->GetPeekRate() - filter_perm->GetPopRate(); i++)
      {
        u32 offset = filter_perm->GetPopRate() + i;
        builder.CreateStore(
          ReadFromChannel(builder, 0),
          builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), builder.getInt32(offset)}));
      }
      builder.CreateBr(after_bb_fill);

      // Switch to new "entry" block.
      func_builder->SwitchBasicBlock(after_bb_fill);
    }
    else
    {
      // Non-readahead peeks. Use a local variable instead of a global, since it doesn't have to be
      // maintained across work function iterations.
      m_input_buffer_var = builder.CreateAlloca(buffer_array_type, nullptr, "peek_buffer");

      // Fill the peek buffer.
      for (u32 i = 0; i < m_input_buffer_size;)
      {
        for (u32 j = 0; j < filter_perm->GetInputChannelWidth(); j++, i++)
        {
          // peek_buffer[i] <- pop()
          builder.CreateStore(
            ReadFromChannel(builder, j),
            builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
        }
      }
    }
  }

  void BuildOutputBuffer(Frontend::FunctionBuilder* func_builder, const StreamGraph::FilterPermutation* filter_perm)
  {
    Frontend::WrappedLLVMContext* context = func_builder->GetContext();
    llvm::IRBuilder<>& builder = func_builder->GetCurrentIRBuilder();
    m_output_buffer_size = filter_perm->GetPushRate();

    llvm::Type* buffer_element_type = filter_perm->GetOutputType();
    llvm::Type* buffer_array_type = llvm::ArrayType::get(buffer_element_type, m_output_buffer_size);

    m_output_buffer_var = builder.CreateAlloca(buffer_array_type, nullptr, "push_buffer");
    m_output_buffer_pos = builder.CreateAlloca(context->GetIntType(), nullptr, "push_buffer_pos");
    builder.CreateStore(builder.getInt32(0), m_output_buffer_pos);
  }

  void BuildEpilogue(Frontend::FunctionBuilder* func_builder, const StreamGraph::FilterPermutation* filter_perm)
  {
    Frontend::WrappedLLVMContext* context = func_builder->GetContext();
    llvm::IRBuilder<>& builder = func_builder->GetCurrentIRBuilder();
    if (!IsOutputBufferingEnabled())
      return;

    for (u32 i = 0; i < m_output_buffer_size;)
    {
      for (u32 j = 0; j < filter_perm->GetOutputChannelWidth(); j++, i++)
      {
        llvm::Value* output_buffer_ptr = builder.CreateInBoundsGEP(
          m_output_buffer_var, {builder.getInt32(0), builder.getInt32(i)}, "output_buffer_ptr");
        WriteToChannel(builder, j, builder.CreateLoad(output_buffer_ptr, "output_buffer_val"));
      }
    }
  }

  llvm::Value* BuildPop(llvm::IRBuilder<>& builder) override final
  {
    if (!IsInputBufferingEnabled())
      return ReadFromChannel(builder, 0);

    // pop_val <- peek_buffer[0]
    llvm::Value* pop_val = builder.CreateLoad(
      builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), builder.getInt32(0)}, "peek_buffer_ptr"),
      "pop_val");

    // peek_buffer[0] = peek_buffer[1], peek_buffer[1] = peek_buffer[2], ...
    for (u32 i = 0; i < (m_input_buffer_size - 1); i++)
    {
      builder.CreateStore(builder.CreateLoad(builder.CreateInBoundsGEP(m_input_buffer_var,
                                                                       {builder.getInt32(0), builder.getInt32(i + 1)})),
                          builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
    }

    return pop_val;
  }

  llvm::Value* BuildPeek(llvm::IRBuilder<>& builder, llvm::Value* idx_value) override final
  {
    // peek_val <- peek_buffer[idx_value]
    llvm::Value* peek_ptr = builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), idx_value}, "peek_ptr");
    return builder.CreateLoad(peek_ptr, "peek_val");
  }

  bool BuildPush(llvm::IRBuilder<>& builder, llvm::Value* value) override final
  {
    if (!IsOutputBufferingEnabled())
    {
      WriteToChannel(builder, 0, value);
      return true;
    }

    llvm::Value* output_buffer_pos = builder.CreateLoad(m_output_buffer_pos, "output_buffer_pos");
    builder.CreateStore(
      value, builder.CreateGEP(m_output_buffer_var, {builder.getInt32(0), output_buffer_pos}, "output_buffer_ptr"));
    builder.CreateStore(builder.CreateAdd(output_buffer_pos, builder.getInt32(1)), m_output_buffer_pos);
    return true;
  }

private:
  std::vector<llvm::Value*> m_in_ptrs;
  std::vector<llvm::Value*> m_out_ptrs;
  llvm::Value* m_input_buffer_var = nullptr;
  llvm::Value* m_output_buffer_var = nullptr;
  llvm::Value* m_output_buffer_pos = nullptr;
  u32 m_input_buffer_size = 0;
  u32 m_output_buffer_size = 0;
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

  if (m_filter_decl->IsBuiltin())
  {
    return GenerateBuiltinFilter();
  }
  else if (m_filter_decl->HasWorkBlock())
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
  std::vector<llvm::Type*> params;
  if (!m_filter_permutation->GetInputType()->isVoidTy())
  {
    llvm::Type* pointer_ty = llvm::PointerType::get(m_filter_permutation->GetInputType(), 0);
    assert(pointer_ty != nullptr);
    for (u32 i = 0; i < m_filter_permutation->GetInputChannelWidth(); i++)
      params.push_back(pointer_ty);
  }
  if (!m_filter_permutation->GetOutputType()->isVoidTy())
  {
    llvm::Type* pointer_ty = llvm::PointerType::get(m_filter_permutation->GetOutputType(), 0);
    assert(pointer_ty != nullptr);
    for (u32 i = 0; i < m_filter_permutation->GetOutputChannelWidth(); i++)
      params.push_back(pointer_ty);
  }

  llvm::FunctionType* func_type = llvm::FunctionType::get(ret_type, params, false);
  llvm::Constant* func_cons = m_module->getOrInsertFunction(name.c_str(), func_type);
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return nullptr;

  // Start at the entry basic block for the work function.
  FragmentBuilder fragment_builder;
  Frontend::FunctionBuilder function_builder(m_context, m_module, &fragment_builder, func);
  fragment_builder.BuildPrologue(&function_builder, m_filter_permutation);

  // Add global variable references
  for (const auto& it : m_filter_permutation->GetFilterParameters())
    function_builder.AddVariable(it.decl, it.value);
  for (const auto& it : m_global_variable_map)
    function_builder.AddVariable(it.first, it.second);

  // Emit code based on the work block.
  if (!block->Accept(&function_builder))
    return nullptr;

  // Final return instruction.
  fragment_builder.BuildEpilogue(&function_builder, m_filter_permutation);
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

  // Evaluate init block if present.
  if (m_filter_decl->HasInitBlock() && !gvb.EvaluateInitBlock(m_filter_permutation))
    return false;

  // And copy the table, ready to insert to the function builders
  m_global_variable_map = gvb.GetVariableMap();
  return true;
}

bool FilterBuilder::GenerateBuiltinFilter()
{
  if (m_filter_decl->GetName().compare(0, 11, "InputReader", 11) == 0 ||
      m_filter_decl->GetName().compare(0, 12, "OutputWriter", 12) == 0)
  {
    // Done at project generation time.
    return true;
  }

  if (m_filter_decl->GetName().compare(0, 8, "Identity", 8) == 0)
    return GenerateBuiltinFilter_Identity();

  Log_ErrorPrintf("Unknown builtin filter '%s'", m_filter_decl->GetName().c_str());
  return false;
}

bool FilterBuilder::GenerateBuiltinFilter_Identity()
{
  std::string function_name = StringFromFormat("filter_%s", m_filter_permutation->GetName().c_str());

  llvm::Type* ret_type = llvm::Type::getVoidTy(m_context->GetLLVMContext());
  std::vector<llvm::Type*> params;
  if (!m_filter_permutation->GetInputType()->isVoidTy())
  {
    llvm::Type* pointer_ty = llvm::PointerType::get(m_filter_permutation->GetInputType(), 0);
    assert(pointer_ty != nullptr);
    for (u32 i = 0; i < m_filter_permutation->GetInputChannelWidth(); i++)
      params.push_back(pointer_ty);
  }
  if (!m_filter_permutation->GetOutputType()->isVoidTy())
  {
    llvm::Type* pointer_ty = llvm::PointerType::get(m_filter_permutation->GetOutputType(), 0);
    assert(pointer_ty != nullptr);
    for (u32 i = 0; i < m_filter_permutation->GetOutputChannelWidth(); i++)
      params.push_back(pointer_ty);
  }

  llvm::FunctionType* func_type = llvm::FunctionType::get(ret_type, params, false);
  m_function = llvm::cast<llvm::Function>(m_module->getOrInsertFunction(function_name, func_type));
  if (!m_function)
    return false;

  m_function->setLinkage(llvm::GlobalValue::PrivateLinkage);

  FragmentBuilder fragment_builder;
  Frontend::FunctionBuilder function_builder(m_context, m_module, &fragment_builder, m_function);
  fragment_builder.BuildPrologue(&function_builder, m_filter_permutation);
  llvm::IRBuilder<>& builder = function_builder.GetCurrentIRBuilder();
  llvm::Value* pop = fragment_builder.BuildPop(builder);
  fragment_builder.BuildPush(builder, pop);
  fragment_builder.BuildEpilogue(&function_builder, m_filter_permutation);
  builder.CreateRetVoid();

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
