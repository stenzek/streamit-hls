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

class ComponentGenerator : private StreamGraph::Visitor
{
public:
  ComponentGenerator(Frontend::WrappedLLVMContext* context, StreamGraph::StreamGraph* streamgraph,
                     const std::string& module_name, llvm::raw_fd_ostream& os);
  ~ComponentGenerator();

  Frontend::WrappedLLVMContext* GetContext() const { return m_context; }
  const std::string& GetModuleName() const { return m_module_name; }

  // Generates the whole module.
  bool GenerateComponent();

private:
  void WriteHeader();
  void WriteFooter();
  void WriteGlobalSignals();
  void WriteFIFOComponentDeclaration();
  void WriteFilterPermutation(const StreamGraph::FilterPermutation* filter);
  void WriteFIFO(const std::string& name, u32 data_width, u32 depth);
  void WriteSplitDuplicate(const StreamGraph::Split* node);
  void WriteSplitRoundrobin(const StreamGraph::Split* node);
  void WriteFilterInstance(StreamGraph::Filter* node);
  void WriteCombinationalFilterInstance(StreamGraph::Filter* node);

  // StreamGraph Visitor Interface
  bool Visit(StreamGraph::Filter* node) override;
  bool Visit(StreamGraph::Pipeline* node) override;
  bool Visit(StreamGraph::SplitJoin* node) override;
  bool Visit(StreamGraph::Split* node) override;
  bool Visit(StreamGraph::Join* node) override;

  Frontend::WrappedLLVMContext* m_context;
  StreamGraph::StreamGraph* m_streamgraph;
  std::string m_module_name;
  llvm::raw_fd_ostream& m_os;
  std::stringstream m_signals;
  std::stringstream m_body;
};

} // namespace HLSTarget