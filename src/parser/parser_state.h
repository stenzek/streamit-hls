#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "parser/ast.h"

class ParserState
{
public:
  using FilterList = std::vector<AST::FilterDeclaration*>;
  using StreamList = std::vector<AST::StreamDeclaration*>;

  ParserState();
  ~ParserState();

  const std::string& GetCurrentFileName() const { return m_current_filename; }

  const std::string& GetEntryPointName() const { return m_entry_point_name; }
  void SetEntryPointName(const std::string& name) { m_entry_point_name = name; }

  AST::LexicalScope* GetGlobalLexicalScope() { return m_global_lexical_scope.get(); }

  const FilterList& GetFilterList() const { return m_filters; }
  const StreamList& GetStreamList() const { return m_streams; }

  bool ParseFile(const char* filename, std::FILE* fp, bool debug);

  void LogError(const char* fmt, ...);
  void LogWarning(const char* fmt, ...);
  void LogInfo(const char* fmt, ...);
  void LogDebug(const char* fmt, ...);

  void LogError(const AST::SourceLocation& loc, const char* fmt, ...);
  void LogWarning(const AST::SourceLocation& loc, const char* fmt, ...);

  void AddFilter(AST::FilterDeclaration* decl);
  void AddStream(AST::StreamDeclaration* decl);
  void AddActiveStream(AST::Node* stream);
  bool HasActiveStream(AST::Node* stream);

  // Mainly for structure types
  bool AddType(const std::string& name, AST::TypeSpecifier* type);
  AST::TypeSpecifier* GetType(const char* name);

  // Array types
  // const AST::TypeSpecifier* GetArrayType(const AST::TypeSpecifier* base_type, const std::vector<AST::Expression*>&
  // array_sizes);

  // Primitive type instances
  AST::TypeSpecifier* GetErrorType() const;
  AST::TypeSpecifier* GetVoidType() const;
  AST::TypeSpecifier* GetBooleanType() const;
  AST::TypeSpecifier* GetBitType() const;
  AST::TypeSpecifier* GetIntType() const;
  AST::TypeSpecifier* GetFloatType() const;
  AST::TypeSpecifier* GetAPIntType(unsigned num_bits) const;

  // Implicit type conversions
  bool CanImplicitlyConvertTo(const AST::TypeSpecifier* from_type, const AST::TypeSpecifier* to_type) const;
  AST::TypeSpecifier* GetResultType(const AST::TypeSpecifier* lhs, const AST::TypeSpecifier* rhs);

  // Filters can't be nested? So this should be sufficient.
  // TODO: Kinda messy though. Maybe would be better placed in the symbol table.
  // TODO: I think they can...
  AST::StreamDeclaration* current_stream = nullptr;
  AST::LexicalScope* current_stream_scope = nullptr;

  // AST dumping/printing
  void DumpAST();

private:
  bool AutoSetEntryPoint(const char* filename);
  void CreateBuiltinTypes();
  void CreateBuiltinFunctions();
  bool SemanticAnalysis();

  std::string m_current_filename;
  std::string m_entry_point_name;

  // Filters and stream lists
  FilterList m_filters;
  StreamList m_streams;
  std::vector<AST::Node*> m_active_streams;

  // Global lexical scope
  std::unique_ptr<AST::LexicalScope> m_global_lexical_scope;

  // Primitive and structure types
  std::map<std::string, AST::TypeSpecifier*> m_types;
  //   const AST::TypeSpecifier* m_error_type = nullptr;
  //   const AST::TypeSpecifier* m_void_type = nullptr;
  //   const AST::TypeSpecifier* m_boolean_type = nullptr;
  //   const AST::TypeSpecifier* m_bit_type = nullptr;
  //   const AST::TypeSpecifier* m_int_type = nullptr;
  //   const AST::TypeSpecifier* m_float_type = nullptr;

  // Array types
  // std::vector<std::pair<std::pair<const Type*, std::vector<AST::Expression*>>, const Type*>> m_array_types;
};

void yyerror(ParserState* state, const char* s);
char* yyasprintf(const char* fmt, ...);
