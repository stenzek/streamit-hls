#pragma once
#include <cstddef>
#include <sstream>
#include "streamgraph/streamgraph.h"

namespace llvm
{
class raw_fd_ostream;
class BasicBlock;
class Constant;
class Function;
class Module;
}

namespace AST
{
class FilterDeclaration;
}

namespace Frontend
{
class WrappedLLVMContext;
}

namespace HLSTarget
{

class ComponentTestBenchGenerator
{
public:
  ComponentTestBenchGenerator(Frontend::WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
                              const std::string& module_name, llvm::raw_fd_ostream& os);
  ~ComponentTestBenchGenerator();

  Frontend::WrappedLLVMContext* GetContext() const { return m_context; }
  const std::string& GetModuleName() const { return m_module_name; }

  // Generates the whole module.
  bool GenerateTestBench();

private:
  void WriteHeader();
  void WriteWrapperComponent();
  void WriteInputGenerator();
  void WriteOutputConsumer();
  void WriteClockGenerator();
  void WriteResetProcess();
  void WriteFooter();

  Frontend::WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_streamgraph;
  std::string m_module_name;
  llvm::raw_fd_ostream& m_os;
  std::stringstream m_signals;
  std::stringstream m_body;
};

} // namespace HLSTarget