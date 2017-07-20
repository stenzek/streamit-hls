#pragma once
#include <unordered_map>

namespace llvm
{
class Constant;
class Function;
class GlobalVariable;
class Module;
}

namespace AST
{
class FilterDeclaration;
class FilterWorkBlock;
class VariableDeclaration;
}

namespace Frontend
{
class Context;

class FilterBuilder
{
public:
  FilterBuilder(Context* context, const AST::FilterDeclaration* filter_decl);
  ~FilterBuilder();

  Context* GetContext() const { return m_context; }
  const AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }
  const std::string& GetNamePrefix() const { return m_name_prefix; }
  llvm::Function* GetInitFunction() const { return m_init_function; }
  llvm::Function* GetPreworkFunction() const { return m_prework_function; }
  llvm::Function* GetWorkFunction() const { return m_work_function; }
  llvm::Constant* GetPeekFunction() const { return m_peek_function; }
  llvm::Constant* GetPopFunction() const { return m_pop_function; }
  llvm::Constant* GetPushFunction() const { return m_push_function; }

  bool GenerateCode();

private:
  llvm::Function* GenerateFunction(AST::FilterWorkBlock* block, const std::string& name);
  bool GenerateGlobals();
  bool GenerateChannelFunctions();

  Context* m_context;
  const AST::FilterDeclaration* m_filter_decl;
  std::string m_name_prefix;
  std::unordered_map<const AST::VariableDeclaration*, llvm::GlobalVariable*> m_global_variable_map;

  llvm::Function* m_init_function = nullptr;
  llvm::Function* m_prework_function = nullptr;
  llvm::Function* m_work_function = nullptr;

  llvm::Constant* m_peek_function = nullptr;
  llvm::Constant* m_pop_function = nullptr;
  llvm::Constant* m_push_function = nullptr;
};

} // namespace Frontend