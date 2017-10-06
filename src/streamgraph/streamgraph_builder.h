#pragma once
#include <cstdarg>
#include <memory>
#include <stack>
#include <unordered_map>
#include <vector>

class ParserState;

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

namespace Frontend
{
class WrappedLLVMContext;
}

namespace StreamGraph
{
class FilterParameters;
class FilterPermutation;
class Node;
}

namespace StreamGraph
{
class BuilderState;

class Builder
{
public:
  Builder(Frontend::WrappedLLVMContext* context, ParserState* state);
  ~Builder();

  std::unique_ptr<BuilderState> GenerateGraph();

private:
  bool GenerateCode();
  bool GenerateGlobals();
  bool GenerateStreamGraphFunctions();
  bool GenerateStreamFunctionPrototype(AST::StreamDeclaration* decl);
  bool GenerateStreamFunction(AST::StreamDeclaration* decl);
  bool GenerateMain();
  bool CreateExecutionEngine();
  void ExecuteMain();

  Frontend::WrappedLLVMContext* m_context;
  ParserState* m_parser_state;
  std::unique_ptr<llvm::Module> m_module;
  std::unordered_map<const AST::StreamDeclaration*, llvm::Function*> m_function_map;
  llvm::ExecutionEngine* m_execution_engine = nullptr;

  std::unique_ptr<BuilderState> m_builder_state;
};

// Methods called by generated code.
class BuilderState
{
public:
  BuilderState(Frontend::WrappedLLVMContext* context, ParserState* state);
  ~BuilderState() = default;

  Node* GetStartNode() const { return m_start_node; }
  const std::vector<FilterPermutation*>& GetFilterPermutations() { return m_filter_permutations; }
  Node* GetProgramInputNode() const { return m_program_input_node; }
  Node* GetProgramOutputNode() const { return m_program_output_node; }

  void AddFilter(const AST::FilterDeclaration* decl, int peek_rate, int pop_rate, int push_rate, va_list ap);
  void BeginPipeline(const AST::PipelineDeclaration* decl);
  void EndPipeline();
  void BeginSplitJoin(const AST::SplitJoinDeclaration* decl);
  void EndSplitJoin();
  void SplitJoinSplit(int mode, const std::vector<int>& distribution);
  void SplitJoinJoin(const std::vector<int>& distribution);

  std::string GenerateName(const std::string& prefix);
  void Error(const char* fmt, ...);

private:
  bool HasTopNode() const;
  Node* GetTopNode();

  void ExtractParameters(FilterParameters* out_params, const AST::StreamDeclaration* stream_decl, va_list ap);

  Frontend::WrappedLLVMContext* m_context;
  ParserState* m_parser_state;
  bool m_error_state = false;

  // Graph building.
  unsigned int m_name_id = 1;
  std::stack<Node*> m_node_stack;
  Node* m_start_node = nullptr;
  Node* m_program_input_node = nullptr;
  Node* m_program_output_node = nullptr;

  // Filter permutations
  std::vector<FilterPermutation*> m_filter_permutations;
};

} // namespace Frontend