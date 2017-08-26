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

namespace StreamGraph
{
class FilterPermutation;
}

namespace HLSTarget
{
class FilterBuilder
{
public:
  FilterBuilder(WrappedLLVMContext* context, llvm::Module* mod, const StreamGraph::FilterPermutation* filter_perm);
  ~FilterBuilder();

  WrappedLLVMContext* GetContext() const { return m_context; }
  const AST::FilterDeclaration* GetFilterDeclaration() const { return m_filter_decl; }
  llvm::Function* GetFunction() const { return m_function; }

  bool GenerateCode();

private:
  llvm::Function* GenerateFunction(AST::FilterWorkBlock* block, const std::string& name);
  bool GenerateGlobals();

  WrappedLLVMContext* m_context;
  llvm::Module* m_module;
  const StreamGraph::FilterPermutation* m_filter_permutation = nullptr;
  const AST::FilterDeclaration* m_filter_decl = nullptr;
  std::unordered_map<const AST::VariableDeclaration*, llvm::GlobalVariable*> m_global_variable_map;

  llvm::Function* m_function = nullptr;
};

} // namespace HLSTarget