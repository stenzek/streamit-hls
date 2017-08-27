#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/type.h"
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
  const Type* AddType(const std::string& name, const Type* type);
  const Type* GetType(const std::string& name);

  // Array types
  const Type* GetArrayType(const Type* base_type, const std::vector<int>& array_sizes);

  // Primitive type instances
  const Type* GetErrorType() const { return m_error_type; }
  const Type* GetBooleanType() const { return m_boolean_type; }
  const Type* GetBitType() const { return m_bit_type; }
  const Type* GetIntType() const { return m_int_type; }
  const Type* GetFloatType() const { return m_float_type; }

  // Implicit type conversions
  const Type* GetResultType(const Type* lhs, const Type* rhs);
  const Type* GetArrayElementType(const ArrayType* ty);

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
