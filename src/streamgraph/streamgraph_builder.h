#pragma once
#include <cstdarg>
#include <memory>
#include <stack>
#include <unordered_map>
#include <vector>

class ParserState;
class WrappedLLVMContext;

namespace llvm
{
class Constant;
class ExecutionEngine;
class Function;
class GlobalVariable;
class Module;
}

namespace AST
{
class StreamDeclaration;
class FilterDeclaration;
class PipelineDeclaration;
class SplitJoinDeclaration;
}

namespace StreamGraph
{
class FilterParameters;
class FilterPermutation;
class Node;
}

namespace StreamGraph
{
class Builder
{
public:
  Builder(WrappedLLVMContext* context, ParserState* state);
  ~Builder();

  Node* GetStartNode() const { return m_start_node; }
  const std::vector<FilterPermutation*>& GetFilterPermutations() { return m_filter_permutations; }

  bool GenerateGraph();

private:
  bool GenerateCode();
  bool GenerateGlobals();
  bool GenerateStreamGraphFunctions();
  bool GenerateStreamFunctionPrototype(AST::StreamDeclaration* decl);
  bool GenerateStreamFunction(AST::StreamDeclaration* decl);
  bool GenerateMain();
  bool CreateExecutionEngine();
  void ExecuteMain();

  WrappedLLVMContext* m_context;
  ParserState* m_parser_state;
  std::unique_ptr<llvm::Module> m_module;
  std::unordered_map<const AST::StreamDeclaration*, llvm::Function*> m_function_map;
  llvm::ExecutionEngine* m_execution_engine = nullptr;

  // Graph building.
  Node* m_start_node = nullptr;
  std::vector<FilterPermutation*> m_filter_permutations;
};

// Methods called by generated code.
class BuilderState
{
public:
  BuilderState(WrappedLLVMContext* context, ParserState* state);
  ~BuilderState() = default;

  Node* GetStartNode() const { return m_start_node; }
  const std::vector<FilterPermutation*>& GetFilterPermutations() { return m_filter_permutations; }

  void AddFilter(const AST::FilterDeclaration* decl, int peek_rate, int pop_rate, int push_rate, va_list ap);
  void BeginPipeline(const AST::PipelineDeclaration* decl);
  void EndPipeline();
  void BeginSplitJoin(const AST::SplitJoinDeclaration* decl);
  void EndSplitJoin();
  void SplitJoinSplit(int mode);
  void SplitJoinJoin();

  std::string GenerateName(const std::string& prefix);
  void Error(const char* fmt, ...);

private:
  bool HasTopNode() const;
  Node* GetTopNode();

  void ExtractParameters(FilterParameters* out_params, const AST::StreamDeclaration* stream_decl, va_list ap);

  WrappedLLVMContext* m_context;
  ParserState* m_parser_state;
  bool m_error_state = false;

  // Graph building.
  unsigned int m_name_id = 1;
  std::stack<Node*> m_node_stack;
  Node* m_start_node = nullptr;

  // Filter permutations
  std::vector<FilterPermutation*> m_filter_permutations;
};

} // namespace Frontend