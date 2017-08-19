#pragma once
#include <unordered_map>

class WrappedLLVMContext;

namespace llvm
{
class Constant;
class Function;
class GlobalVariable;
class Module;
class Type;
}

namespace AST
{
class FilterDeclaration;
class FilterWorkBlock;
class VariableDeclaration;
}

namespace HLSTarget
{
class FilterBuilder
{
public:
  FilterBuilder(WrappedLLVMContext* context, llvm::Module* mod, const AST::FilterDeclaration* filter_decl);
  ~FilterBuilder();

  WrappedLLVMContext* GetContext() const { return m_context; }
  const AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }
  const std::string& GetNamePrefix() const { return m_instance_name; }
  llvm::Function* GetFunction() const { return m_function; }

  bool GenerateCode();

private:
  llvm::Function* GenerateFunction(AST::FilterWorkBlock* block, const std::string& name);
  bool GenerateGlobals();

  WrappedLLVMContext* m_context;
  llvm::Module* m_module;
  const AST::FilterDeclaration* m_filter_decl = nullptr;
  std::string m_instance_name;
  std::unordered_map<const AST::VariableDeclaration*, llvm::GlobalVariable*> m_global_variable_map;

  llvm::Function* m_function = nullptr;
};

} // namespace HLSTarget