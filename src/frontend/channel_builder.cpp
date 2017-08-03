#include "frontend/channel_builder.h"
#include <cassert>
#include <vector>
#include "common/string_helpers.h"
#include "frontend/context.h"
#include "frontend/stream_graph.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "parser/ast.h"
#include "parser/type.h"

namespace Frontend
{
constexpr unsigned int FIFO_QUEUE_SIZE = 64;

ChannelBuilder::ChannelBuilder(Context* context, llvm::Module* mod, const std::string& instance_name)
  : m_context(context), m_module(mod), m_instance_name(instance_name)
{
}

ChannelBuilder::~ChannelBuilder()
{
}

bool ChannelBuilder::GenerateCode(StreamGraph::Filter* filter)
{
  if (filter->GetFilterDeclaration()->GetInputType()->IsVoid())
    return true;

  return (GenerateFilterGlobals(filter) && GenerateFilterPeekFunction(filter) && GenerateFilterPopFunction(filter) &&
          GenerateFilterPushFunction(filter));
}

bool ChannelBuilder::GenerateCode(StreamGraph::Split* split, int mode)
{
  return (GenerateSplitGlobals(split, mode) && GenerateSplitPushFunction(split, mode));
}

bool ChannelBuilder::GenerateCode(StreamGraph::Join* join)
{
  return (GenerateJoinGlobals(join) && GenerateJoinSyncFunction(join) && GenerateJoinPushFunction(join));
}

bool ChannelBuilder::GenerateFilterGlobals(StreamGraph::Filter* filter)
{
  llvm::Type* data_ty = m_context->GetLLVMType(filter->GetFilterDeclaration()->GetInputType());

  // Create struct type
  //
  // data_type data[FIFO_QUEUE_SIZE]
  // int head
  // int tail
  // int size
  //
  llvm::ArrayType* data_array_ty = llvm::ArrayType::get(data_ty, FIFO_QUEUE_SIZE);
  m_input_buffer_type =
    llvm::StructType::create(StringFromFormat("%s_buf_type", m_instance_name.c_str()), data_array_ty,
                             m_context->GetIntType(), m_context->GetIntType(), m_context->GetIntType(), nullptr);

  // Create global variable
  m_input_buffer_var = new llvm::GlobalVariable(*m_module, m_input_buffer_type, true, llvm::GlobalValue::PrivateLinkage,
                                                nullptr, StringFromFormat("%s_buf", m_instance_name.c_str()));

  // Initializer for global variable
  llvm::ConstantAggregateZero* buffer_initializer = llvm::ConstantAggregateZero::get(m_input_buffer_type);
  m_input_buffer_var->setConstant(false);
  m_input_buffer_var->setInitializer(buffer_initializer);
  return true;
}

bool ChannelBuilder::GenerateFilterPeekFunction(StreamGraph::Filter* filter)
{
  llvm::Type* ret_ty = m_context->GetLLVMType(filter->GetFilterDeclaration()->GetInputType());
  llvm::FunctionType* llvm_peek_fn = llvm::FunctionType::get(ret_ty, {m_context->GetIntType()}, false);
  llvm::Constant* func_cons =
    m_module->getOrInsertFunction(StringFromFormat("%s_peek", m_instance_name.c_str()), llvm_peek_fn);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);

  auto func_args_iter = func->arg_begin();
  llvm::Value* index = &(*func_args_iter++);
  index->setName("index");

  // tail_ptr = &buf.tail
  // pos_1 = *tail_ptr
  llvm::Value* tail_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(2)}, "tail_ptr");
  llvm::Value* pos_1 = builder.CreateLoad(tail_ptr, "pos_1");

  // pos = (pos_1 + index) % FIFO_QUEUE_SIZE
  llvm::Value* pos_2 = builder.CreateAdd(pos_1, index, "pos_2");
  llvm::Value* pos = builder.CreateURem(pos_2, builder.getInt32(FIFO_QUEUE_SIZE), "tail");

  // value_ptr = &buf.data[pos]
  // value = *value_ptr
  llvm::Value* value_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                     {builder.getInt32(0), builder.getInt32(0), pos}, "value_ptr");
  llvm::Value* value = builder.CreateLoad(value_ptr, "value");

  // return value
  builder.CreateRet(value);
  return true;
}

bool ChannelBuilder::GenerateFilterPopFunction(StreamGraph::Filter* filter)
{
  llvm::Type* ret_ty = m_context->GetLLVMType(filter->GetFilterDeclaration()->GetInputType());
  llvm::FunctionType* llvm_pop_fn = llvm::FunctionType::get(ret_ty, false);
  llvm::Constant* func_cons =
    m_module->getOrInsertFunction(StringFromFormat("%s_pop", m_instance_name.c_str()), llvm_pop_fn);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);

  // tail_ptr = &buf.tail
  // tail = *tail_ptr
  llvm::Value* tail_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(2)}, "tail_ptr");
  llvm::Value* tail = builder.CreateLoad(tail_ptr, "tail");

  // value_ptr = &buf.data[tail]
  // value = *value_ptr
  llvm::Value* value_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                     {builder.getInt32(0), builder.getInt32(0), tail}, "value_ptr");
  llvm::Value* value = builder.CreateLoad(value_ptr, "value");

  // new_tail = (tail + 1) % FIFO_QUEUE_SIZE
  llvm::Value* new_tail_1 = builder.CreateAdd(tail, builder.getInt32(1), "new_tail_1");
  llvm::Value* new_tail = builder.CreateURem(new_tail_1, builder.getInt32(FIFO_QUEUE_SIZE), "new_tail");

  // *tail_ptr = new_tail
  builder.CreateStore(new_tail, tail_ptr);

  // size_ptr = &buf.size
  // size_1 = *size_ptr
  // size_2 = size_1 - 1
  // *size_ptr = size_2
  llvm::Value* size_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(3)}, "size_ptr");
  llvm::Value* size_1 = builder.CreateLoad(size_ptr, "size");
  llvm::Value* size_2 = builder.CreateSub(size_1, builder.getInt32(1), "size");
  builder.CreateStore(size_2, size_ptr);

  // return value
  builder.CreateRet(value);
  return true;
}

bool ChannelBuilder::GenerateFilterPushFunction(StreamGraph::Filter* filter)
{
  llvm::Type* param_ty = m_context->GetLLVMType(filter->GetFilterDeclaration()->GetInputType());
  llvm::FunctionType* llvm_push_fn = llvm::FunctionType::get(m_context->GetVoidType(), {param_ty}, false);
  llvm::Constant* func_cons =
    m_module->getOrInsertFunction(StringFromFormat("%s_push", m_instance_name.c_str()), llvm_push_fn);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);

  auto func_args_iter = func->arg_begin();
  llvm::Value* value = &(*func_args_iter++);
  value->setName("value");

  // head_ptr = &buf.head
  // head = *head_ptr
  llvm::Value* head_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(1)}, "head_ptr");
  llvm::Value* head = builder.CreateLoad(head_ptr, "head");

  // value_ptr = &buf.data[head]
  // value_ptr = *value_ptr
  llvm::Value* value_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                     {builder.getInt32(0), builder.getInt32(0), head}, "value_ptr");
  builder.CreateStore(value, value_ptr);

  // new_head = (head + 1) % FIFO_QUEUE_SIZE
  // *head_ptr = new_head
  llvm::Value* new_head_1 = builder.CreateAdd(head, builder.getInt32(1), "new_head_1");
  llvm::Value* new_head = builder.CreateURem(new_head_1, builder.getInt32(FIFO_QUEUE_SIZE), "new_head");
  builder.CreateStore(new_head, head_ptr);

  // size_ptr = &buf.size
  // size_1 = *size_ptr
  // size_2 = size_1 + 1
  // *size_ptr = size_2
  llvm::Value* size_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(3)}, "size_ptr");
  llvm::Value* size_1 = builder.CreateLoad(size_ptr, "size");
  llvm::Value* size_2 = builder.CreateAdd(size_1, builder.getInt32(1), "size");
  builder.CreateStore(size_2, size_ptr);
  builder.CreateRetVoid();
  return true;
}

bool ChannelBuilder::GenerateSplitGlobals(StreamGraph::Split* split, int mode)
{
  if (mode == 0)
  {
    // roundrobin - we need a last index variable
    m_last_index_var =
      new llvm::GlobalVariable(*m_module, m_context->GetIntType(), true, llvm::GlobalValue::PrivateLinkage, nullptr,
                               StringFromFormat("%s_last_index", m_instance_name.c_str()));
    m_last_index_var->setConstant(false);
    m_last_index_var->setInitializer(llvm::ConstantInt::get(m_context->GetIntType(), 0));
  }

  return true;
}

bool ChannelBuilder::GenerateSplitPushFunction(StreamGraph::Split* split, int mode)
{
  // if mode is roundrobin
  //     last_index = (last_index + 1) % num_outputs
  //     switch (last_index)
  //       for_each_output case:
  //         output_name_push(data);
  // else
  //     for_each_output_case
  //       output_name_push(data)
  //

  assert(split->GetInputType() == split->GetOutputType());
  llvm::Type* data_ty = m_context->GetLLVMType(split->GetOutputType());

  // Get output function prototypes
  std::vector<llvm::Constant*> output_functions;
  for (const std::string& output_name : split->GetOutputChannelNames())
  {
    llvm::Constant* func = m_module->getOrInsertFunction(StringFromFormat("%s_push", output_name.c_str()),
                                                         m_context->GetVoidType(), data_ty, nullptr);
    if (!func)
    {
      m_context->LogError("Failed to get function pointer '%s_push'", output_name.c_str());
      return false;
    }

    output_functions.push_back(func);
  }

  // Create split push function
  llvm::Constant* func_cons = m_module->getOrInsertFunction(StringFromFormat("%s_push", m_instance_name.c_str()),
                                                            m_context->GetVoidType(), data_ty, nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);

  auto func_args_iter = func->arg_begin();
  llvm::Value* value = &(*func_args_iter++);
  value->setName("value");

  if (mode == 0)
  {
    // TODO: roundrobin
  }
  else
  {
    // duplicate
    for (llvm::Constant* output_func : output_functions)
      builder.CreateCall(output_func, {value});
  }

  builder.CreateRetVoid();
  return true;
}

bool ChannelBuilder::GenerateJoinGlobals(StreamGraph::Join* join)
{
  u32 num_inputs = join->GetIncomingStreams();
  assert(num_inputs > 0);

  // input buffer struct
  //    int next_input
  //    int heads[num_inputs]
  //    int tails[num_inputs]
  //    int sizes[num_inputs]
  //    data_type buf[num_inputs][FIFO_QUEUE_SIZE]

  assert(join->GetInputType() == join->GetInputType());
  llvm::Type* data_ty = m_context->GetLLVMType(join->GetInputType());
  llvm::ArrayType* data_array_ty = llvm::ArrayType::get(data_ty, FIFO_QUEUE_SIZE);
  llvm::ArrayType* buf_array_ty = llvm::ArrayType::get(data_array_ty, num_inputs);
  llvm::ArrayType* int_array_ty = llvm::ArrayType::get(m_context->GetIntType(), num_inputs);
  m_input_buffer_type =
    llvm::StructType::create(StringFromFormat("%s_buf_type", m_instance_name.c_str()), m_context->GetIntType(),
                             int_array_ty, int_array_ty, int_array_ty, buf_array_ty, nullptr);
  if (!m_input_buffer_type)
    return false;

  // Create global variable
  m_input_buffer_var = new llvm::GlobalVariable(*m_module, m_input_buffer_type, true, llvm::GlobalValue::PrivateLinkage,
                                                nullptr, StringFromFormat("%s_buf", m_instance_name.c_str()));

  // Initializer for global variable
  llvm::ConstantAggregateZero* buffer_initializer = llvm::ConstantAggregateZero::get(m_input_buffer_type);
  m_input_buffer_var->setConstant(false);
  m_input_buffer_var->setInitializer(buffer_initializer);
  return true;
}

bool ChannelBuilder::GenerateJoinSyncFunction(StreamGraph::Join* join)
{
  u32 num_inputs = join->GetIncomingStreams();
  assert(num_inputs > 0);

  llvm::Type* data_ty = m_context->GetLLVMType(join->GetOutputType());
  llvm::Constant* output_func = m_module->getOrInsertFunction(
    StringFromFormat("%s_push", join->GetOutputChannelName().c_str()), m_context->GetVoidType(), data_ty, nullptr);
  if (!output_func)
  {
    m_context->LogError("Failed to get output function '%s_push'", join->GetOutputChannelName().c_str());
    return false;
  }

  llvm::Constant* func_cons = m_module->getOrInsertFunction(StringFromFormat("%s_sync", m_instance_name.c_str()),
                                                            m_context->GetVoidType(), nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry_bb", func);
  llvm::BasicBlock* compare_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "compare_bb", func);
  llvm::BasicBlock* loop_body_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "loop_body", func);
  llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "exit", func);

  llvm::IRBuilder<> builder(entry_bb);
  builder.CreateBr(compare_bb);

  // next_input_ptr = &buf.next_input
  // next_input = *next_input_ptr
  builder.SetInsertPoint(compare_bb);
  llvm::Value* next_input_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                          {builder.getInt32(0), builder.getInt32(0)}, "next_input_ptr");
  llvm::Value* next_input = builder.CreateLoad(next_input_ptr, "next_input");

  // size_ptr = &buf.sizes[next_input]
  // size = *size_ptr
  llvm::Value* size_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(3), next_input}, "size_ptr");
  llvm::Value* size = builder.CreateLoad(size_ptr, "size");

  // (size == 0) ? goto exit : goto loop_body;
  llvm::Value* comp = builder.CreateICmpEQ(size, builder.getInt32(0), "size_eq_zero");
  builder.CreateCondBr(comp, exit_bb, loop_body_bb);

  // loop_body:
  builder.SetInsertPoint(loop_body_bb);

  // tail_ptr = &buf.tails[next_input]
  // tail = *tail_ptr
  llvm::Value* tail_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(2), next_input}, "tail_ptr");
  llvm::Value* tail = builder.CreateLoad(tail_ptr, "tail");

  // value_ptr = &buf.data[next_input][tail]
  // value = *value_ptr
  llvm::Value* value_ptr = builder.CreateInBoundsGEP(
    m_input_buffer_type, m_input_buffer_var, {builder.getInt32(0), builder.getInt32(4), next_input, tail}, "value_ptr");
  llvm::Value* value = builder.CreateLoad(value_ptr, "value");

  // tail = (tail + 1) % FIFO_QUEUE_SIZE
  tail = builder.CreateAdd(tail, builder.getInt32(1), "tail");
  tail = builder.CreateURem(tail, builder.getInt32(FIFO_QUEUE_SIZE), "tail");
  builder.CreateStore(tail, tail_ptr);

  // size = size - 1
  size = builder.CreateSub(size, builder.getInt32(1), "size");
  builder.CreateStore(size, size_ptr);

  // next_input = (next_input + 1) % num_inputs
  next_input = builder.CreateAdd(next_input, builder.getInt32(1), "next_input");
  next_input = builder.CreateURem(next_input, builder.getInt32(num_inputs), "next_input");
  builder.CreateStore(next_input, next_input_ptr);

  // call output_stream_name_push(value)
  builder.CreateCall(output_func, {value});

  // goto compare_bb
  builder.CreateBr(compare_bb);

  // exit:
  builder.SetInsertPoint(exit_bb);
  builder.CreateRetVoid();
  return true;
}

bool ChannelBuilder::GenerateJoinPushFunction(StreamGraph::Join* join)
{
  llvm::Type* data_ty = m_context->GetLLVMType(join->GetOutputType());

  // Look up sync function, since we need to call it
  llvm::Constant* sync_func = m_module->getOrInsertFunction(StringFromFormat("%s_sync", m_instance_name.c_str()),
                                                            m_context->GetVoidType(), nullptr);
  if (!sync_func)
  {
    m_context->LogError("Failed to get sync function '%s_sync'", m_instance_name.c_str());
    return false;
  }

  // Generate the main push function (which takes an additional parameter for the source stream)
  llvm::Constant* push_func =
    m_module->getOrInsertFunction(StringFromFormat("%s_push", m_instance_name.c_str()), m_context->GetVoidType(),
                                  m_context->GetIntType(), data_ty, nullptr);
  if (!push_func)
    return false;
  {
    llvm::Function* func = llvm::cast<llvm::Function>(push_func);
    if (!func)
      return false;

    llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
    llvm::IRBuilder<> builder(entry_bb);

    auto func_args_iter = func->arg_begin();
    llvm::Value* src_stream = &(*func_args_iter++);
    llvm::Value* value = &(*func_args_iter++);
    src_stream->setName("src_stream");
    value->setName("value");

    // head_ptr = &buf.heads[src_stream]
    // head = *head_ptr
    llvm::Value* head_ptr = builder.CreateInBoundsGEP(
      m_input_buffer_type, m_input_buffer_var, {builder.getInt32(0), builder.getInt32(1), src_stream}, "head_ptr");
    llvm::Value* head = builder.CreateLoad(head_ptr, "head");

    // value_ptr = &buf.data[head]
    // value_ptr = *value_ptr
    llvm::Value* value_ptr =
      builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                {builder.getInt32(0), builder.getInt32(4), src_stream, head}, "value_ptr");
    builder.CreateStore(value, value_ptr);

    // new_head = (head + 1) % FIFO_QUEUE_SIZE
    // *head_ptr = new_head
    llvm::Value* new_head_1 = builder.CreateAdd(head, builder.getInt32(1), "new_head_1");
    llvm::Value* new_head = builder.CreateURem(new_head_1, builder.getInt32(FIFO_QUEUE_SIZE), "new_head");
    builder.CreateStore(new_head, head_ptr);

    // size_ptr = &buf.size
    // size_1 = *size_ptr
    // size_2 = size_1 + 1
    // *size_ptr = size_2
    llvm::Value* size_ptr = builder.CreateInBoundsGEP(
      m_input_buffer_type, m_input_buffer_var, {builder.getInt32(0), builder.getInt32(3), src_stream}, "size_ptr");
    llvm::Value* size_1 = builder.CreateLoad(size_ptr, "size");
    llvm::Value* size_2 = builder.CreateAdd(size_1, builder.getInt32(1), "size");
    builder.CreateStore(size_2, size_ptr);

    // TODO: This call can be skipped when next_input != src_stream.
    builder.CreateCall(sync_func);
    builder.CreateRetVoid();
  }

  // Generate the push function wrappers for each source stream
  // Yeah, this has another level of indirection, but it should get inlined anyway.
  for (u32 source_stream_index = 1; source_stream_index <= join->GetIncomingStreams(); source_stream_index++)
  {
    llvm::Constant* func_cons =
      m_module->getOrInsertFunction(StringFromFormat("%s_%u_push", m_instance_name.c_str(), source_stream_index),
                                    m_context->GetVoidType(), data_ty, nullptr);
    if (!func_cons)
      return false;

    llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
    if (!func)
      return false;

    llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
    llvm::IRBuilder<> builder(entry_bb);

    auto func_args_iter = func->arg_begin();
    llvm::Value* value = &(*func_args_iter++);
    value->setName("value");

    builder.CreateCall(push_func, {builder.getInt32(i32(source_stream_index)), value});
    builder.CreateRetVoid();
  }

  return true;
}

} // namespace Frontend
