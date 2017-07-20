#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "parser/ast.h"
#include "parser/type.h"

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

  bool ParseFile(const char* filename, std::FILE* fp);

  void ReportError(const char* fmt, ...);
  void ReportError(const AST::SourceLocation& loc, const char* fmt, ...);

  void AddFilter(AST::FilterDeclaration* decl);
  void AddStream(AST::StreamDeclaration* decl);
  void AddActiveStream(AST::Node* stream);
  bool HasActiveStream(AST::Node* stream);

  // Mainly for structure types
  const Type* AddType(const std::string& name, const Type* type);
  const Type* GetType(const std::string& name);

  // Array types
  const Type* GetArrayType(const Type* base_type, const std::vector<int>& array_sizes);

  // Primitive type instances
  const Type* GetErrorType();
  const Type* GetBooleanType();
  const Type* GetBitType();
  const Type* GetIntType();
  const Type* GetFloatType();

  // Filters can't be nested? So this should be sufficient.
  // TODO: Kinda messy though. Maybe would be better placed in the symbol table.
  // TODO: I think they can...
  AST::FilterDeclaration* current_filter = nullptr;

  // AST dumping/printing
  void DumpAST();

private:
  bool AutoSetEntryPoint(const char* filename);
  void CreateBuiltinTypes();
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
  std::map<std::string, const Type*> m_types;
  const Type* m_error_type = nullptr;
  const Type* m_void_type = nullptr;
  const Type* m_boolean_type = nullptr;
  const Type* m_bit_type = nullptr;
  const Type* m_int_type = nullptr;
  const Type* m_float_type = nullptr;

  // Array types
  std::vector<std::pair<std::pair<const Type*, std::vector<int>>, const Type*>> m_array_types;
};

void yyerror(ParserState* state, const char* s);
char* yyasprintf(const char* fmt, ...);
