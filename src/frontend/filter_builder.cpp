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
    m_output_instance_name(output_instance_name)
{
}

FilterBuilder::~FilterBuilder()
{
}

bool FilterBuilder::GenerateCode()
{
  if (!GenerateGlobals() || !GenerateChannelFunctions())
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

bool FilterBuilder::GenerateChannelFunctions()
{
  // Pop/peek
  if (!m_filter_decl->GetInputType()->IsVoid())
  {
    if (!GenerateInputBuffer())
      return false;

    if (!GenerateInputPeekFunction() || !GenerateInputPopFunction() || !GenerateInputPushFunction())
      return false;

    if (!GenerateInputGetSizeFunction() || !GenerateInputGetSpaceFunction())
      return false;
  }

  // Push - this needs the name of the output filter
  if (!m_filter_decl->GetOutputType()->IsVoid() && !GenerateOutputPushPrototype())
    return false;

  return true;
}

constexpr unsigned int FIFO_QUEUE_SIZE = 64;

bool FilterBuilder::GenerateInputBuffer()
{
  llvm::Type* data_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());

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

bool FilterBuilder::GenerateInputPeekFunction()
{
  llvm::Type* ret_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
  llvm::FunctionType* llvm_peek_fn = llvm::FunctionType::get(ret_ty, {m_context->GetIntType()}, false);
  m_peek_function = m_module->getOrInsertFunction(StringFromFormat("%s_peek", m_instance_name.c_str()), llvm_peek_fn);
  if (!m_peek_function)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(m_peek_function);
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

bool FilterBuilder::GenerateInputPopFunction()
{
  llvm::Type* ret_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
  llvm::FunctionType* llvm_pop_fn = llvm::FunctionType::get(ret_ty, false);
  m_pop_function = m_module->getOrInsertFunction(StringFromFormat("%s_pop", m_instance_name.c_str()), llvm_pop_fn);
  if (!m_pop_function)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(m_pop_function);
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

bool FilterBuilder::GenerateInputPushFunction()
{
  llvm::Type* param_ty = m_context->GetLLVMType(m_filter_decl->GetInputType());
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

bool FilterBuilder::GenerateInputGetSizeFunction()
{
  std::string func_name = StringFromFormat("%s_getsize", m_instance_name.c_str());
  llvm::Constant* func_cons = m_module->getOrInsertFunction(func_name, m_context->GetIntType(), nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);

  // size_ptr = &buf.size
  // size = *size_ptr
  // return size
  llvm::Value* size_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(3)}, "size_ptr");
  llvm::Value* size = builder.CreateLoad(size_ptr, "size");
  builder.CreateRet(size);
  return true;
}

bool FilterBuilder::GenerateInputGetSpaceFunction()
{
  std::string func_name = StringFromFormat("%s_getspace", m_instance_name.c_str());
  llvm::Constant* func_cons = m_module->getOrInsertFunction(func_name, m_context->GetIntType(), nullptr);
  if (!func_cons)
    return false;
  llvm::Function* func = llvm::cast<llvm::Function>(func_cons);
  if (!func)
    return false;

  llvm::BasicBlock* entry_bb = llvm::BasicBlock::Create(m_context->GetLLVMContext(), "entry", func);
  llvm::IRBuilder<> builder(entry_bb);

  // size_ptr = &buf.size
  // size_1 = *size_ptr
  // size_2 = FIFO_QUEUE_SIZE - size_1
  // return size_2
  llvm::Value* size_ptr = builder.CreateInBoundsGEP(m_input_buffer_type, m_input_buffer_var,
                                                    {builder.getInt32(0), builder.getInt32(3)}, "size_ptr");
  llvm::Value* size_1 = builder.CreateLoad(size_ptr, "size");
  llvm::Value* size_2 = builder.CreateSub(builder.getInt32(FIFO_QUEUE_SIZE), size_1, "size");
  builder.CreateRet(size_2);
  return true;
}

bool FilterBuilder::GenerateOutputPushPrototype()
{
  llvm::Type* param_ty = m_context->GetLLVMType(m_filter_decl->GetOutputType());
  llvm::FunctionType* llvm_push_fn = llvm::FunctionType::get(m_context->GetVoidType(), {param_ty}, false);
  m_push_function = m_module->getOrInsertFunction(StringFromFormat("%s_push", m_instance_name.c_str()), llvm_push_fn);
  return (m_push_function != nullptr);
}

} // namespace Frontend
