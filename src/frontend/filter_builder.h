#pragma once
#include <unordered_map>

namespace llvm
{
class Function;
class Module;
}

namespace AST
{
class FilterDeclaration;
class FilterWorkBlock;
}

namespace Frontend
{
class Context;

class FilterBuilder
{
public:
  FilterBuilder(Context* context, llvm::Module* module, AST::FilterDeclaration* filter_decl);
  ~FilterBuilder();

  llvm::Function* GetInitFunction() const
  {
    return m_init_function;
  }
  llvm::Function* GetPreworkFunction() const
  {
    return m_prework_function;
  }
  llvm::Function* GetWorkFunction() const
  {
    return m_work_function;
  }

  bool GenerateCode();

private:
  llvm::Function* GenerateFunction(AST::FilterWorkBlock* block, const std::string& name);

  Context* m_context;
  llvm::Module* m_module;
  const AST::FilterDeclaration* m_filter_decl;

  llvm::Function* m_init_function = nullptr;
  llvm::Function* m_prework_function = nullptr;
  llvm::Function* m_work_function = nullptr;
};

} // namespace Frontend