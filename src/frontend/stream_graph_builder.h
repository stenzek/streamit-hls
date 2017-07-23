#pragma once
#include <memory>
#include <stack>
#include <unordered_map>

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
}
namespace Frontend
{
class Context;
}

namespace StreamGraph
{
class Node;
}

class ParserState;

namespace StreamGraph
{
class Builder
{
public:
  Builder(Frontend::Context* context, ParserState* state);
  ~Builder();

  Node* GetStartNode() const { return m_start_node; }

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

  Frontend::Context* m_context;
  ParserState* m_parser_state;
  std::unique_ptr<llvm::Module> m_module;
  std::unordered_map<const AST::StreamDeclaration*, llvm::Function*> m_function_map;
  llvm::ExecutionEngine* m_execution_engine = nullptr;

  // Graph building.
  Node* m_start_node = nullptr;
};

// Methods called by generated code.
class BuilderState
{
public:
  BuilderState(ParserState* state);
  ~BuilderState() = default;

  Node* GetStartNode() const { return m_start_node; }

  void AddFilter(const char* name);
  void BeginPipeline(const char* name);
  void EndPipeline(const char* name);
  void BeginSplitJoin(const char* name);
  void EndSplitJoin(const char* name);
  void Split(int mode);
  void Join();

  std::string GenerateName(const char* prefix);
  void Error(const char* fmt, ...);

private:
  bool HasTopNode() const;
  Node* GetTopNode();

  ParserState* m_parser_state;
  bool m_error_state = false;

  // Graph building.
  unsigned int m_name_id = 1;
  std::stack<Node*> m_node_stack;
  Node* m_start_node = nullptr;
};

} // namespace Frontend