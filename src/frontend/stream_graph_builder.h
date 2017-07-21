#pragma once
#include <memory>
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

class ParserState;

namespace Frontend
{
class Context;

class StreamGraphBuilder
{
public:
  StreamGraphBuilder(Context* context, ParserState* state);
  ~StreamGraphBuilder();

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

  Context* m_context;
  ParserState* m_parser_state;
  std::unique_ptr<llvm::Module> m_module;
  std::unordered_map<const AST::StreamDeclaration*, llvm::Function*> m_function_map;
  llvm::ExecutionEngine* m_execution_engine = nullptr;
};

} // namespace Frontend