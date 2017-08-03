#include "frontend/channel_builder.h"
#include <cassert>
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
  return (GenerateJoinGlobals(join) && GenerateJoinPushFunction(join));
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
  return true;
}

bool ChannelBuilder::GenerateSplitPushFunction(StreamGraph::Split* split, int mode)
{
  return true;
}

bool ChannelBuilder::GenerateJoinGlobals(StreamGraph::Join* join)
{
  return true;
}

bool ChannelBuilder::GenerateJoinPushFunction(StreamGraph::Join* join)
{
  return true;
}

} // namespace Frontend
