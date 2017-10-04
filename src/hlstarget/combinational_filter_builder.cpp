#include "hlstarget/combinational_filter_builder.h"
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
#include "parser/ast.h"
#include "streamgraph/streamgraph.h"
Log_SetChannel(HLSTarget::CombinationalFilterBuilder);

namespace HLSTarget
{
struct CombinationalFragmentBuilder : public Frontend::FunctionBuilder::TargetFragmentBuilder
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
        arg->setName(StringFromFormat("in_ptr_%u", i));
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
        arg->setName(StringFromFormat("out_ptr_%u", i));
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

    llvm::Type* buffer_element_type = filter_perm->GetInputType();
    llvm::Type* buffer_array_type = llvm::ArrayType::get(buffer_element_type, m_input_buffer_size);

    // Non-readahead peeks. Use a local variable instead of a global, since it doesn't have to be
    // maintained across work function iterations.
    m_input_buffer_var = builder.CreateAlloca(buffer_array_type, nullptr, "peek_buffer");

    // Fill the peek buffer.
    for (u32 i = 0; i < m_input_buffer_size;)
    {
      for (u32 j = 0; j < filter_perm->GetInputChannelWidth(); j++, i++)
      {
        // peek_buffer[i] <- pop()
        builder.CreateStore(ReadFromChannel(builder, j),
                            builder.CreateInBoundsGEP(m_input_buffer_var, {builder.getInt32(0), builder.getInt32(i)}));
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

CombinationalFilterBuilder::CombinationalFilterBuilder(Frontend::WrappedLLVMContext* context, llvm::Module* mod,
                                                       const StreamGraph::FilterPermutation* filter_perm)
  : m_context(context), m_module(mod), m_filter_permutation(filter_perm),
    m_filter_decl(filter_perm->GetFilterDeclaration())
{
}

CombinationalFilterBuilder::~CombinationalFilterBuilder()
{
}

bool CombinationalFilterBuilder::GenerateCode()
{
  if (!GenerateGlobals())
    return false;

  if (m_filter_decl->HasWorkBlock())
  {
    std::string name = StringFromFormat("comb_filter_%s", m_filter_permutation->GetName().c_str());
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

llvm::Function* CombinationalFilterBuilder::GenerateFunction(AST::FilterWorkBlock* block, const std::string& name)
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
  CombinationalFragmentBuilder fragment_builder;
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

bool CombinationalFilterBuilder::GenerateGlobals()
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

} // namespace HLSTarget
