#pragma once
#include <cstddef>
#include <sstream>
#include "streamgraph/streamgraph.h"

class WrappedLLVMContext;

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

namespace HLSTarget
{

class ComponentGenerator : private StreamGraph::Visitor
{
public:
  ComponentGenerator(WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph, const std::string& module_name,
                     llvm::raw_fd_ostream& os);
  ~ComponentGenerator();

  WrappedLLVMContext* GetContext() const { return m_context; }
  const std::string& GetModuleName() const { return m_module_name; }

  // Generates the whole module.
  bool GenerateComponent();

private:
  void WriteHeader();
  void WriteFooter();
  void WriteGlobalSignals();
  void WriteFIFOComponentDeclaration();
  void WriteFilterPermutation(const StreamGraph::FilterPermutation* filter);

  // StreamGraph Visitor Interface
  bool Visit(StreamGraph::Filter* node) override;
  bool Visit(StreamGraph::Pipeline* node) override;
  bool Visit(StreamGraph::SplitJoin* node) override;
  bool Visit(StreamGraph::Split* node) override;
  bool Visit(StreamGraph::Join* node) override;

  WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_streamgraph;
  std::string m_module_name;
  llvm::raw_fd_ostream& m_os;
  std::stringstream m_signals;
  std::stringstream m_body;
};

} // namespace HLSTarget