#pragma once
#include <list>
#include <string>
#include <vector>
#include "parser/symbol_table.h"

class ASTPrinter;
class CodeGenerator;
class ParserState;

namespace AST
{
class Visitor;
class Node;
class NodeList;
class TypeSpecifier;
class Statement;
class Declaration;
class Expression;
class StreamDeclaration;
class FilterDeclaration;
class FilterWorkBlock;
class IntegerLiteralExpression;
class IdentifierExpression;
class BinaryExpression;
class AssignmentExpression;
class PeekExpression;
class PopExpression;
class PushStatement;
class VariableDeclaration;
class ExpressionStatement;

using LexicalScope = SymbolTable<std::string, AST::Node>;
using ExpressionList = std::vector<Expression*>;

struct SourceLocation
{
  const char* filename;
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};

class StringList
{
public:
  using ListType = std::vector<std::string>;

  StringList() = default;
  ~StringList() = default;

  const std::string& operator[](size_t index) const { return m_values[index]; }
  ListType::const_iterator begin() const { return m_values.begin(); }
  ListType::const_iterator end() const { return m_values.end(); }

  std::string& operator[](size_t index) { return m_values[index]; }
  ListType::iterator begin() { return m_values.begin(); }
  ListType::iterator end() { return m_values.end(); }

  void AddString(const char* str) { m_values.emplace_back(str); }
  void AddString(const std::string& str) { m_values.push_back(str); }

private:
  ListType m_values;
};

class Node
{
public:
  virtual ~Node() = default;
  virtual void Dump(ASTPrinter* printer) const = 0;
  virtual bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) = 0;
  virtual bool Accept(Visitor* visitor) = 0;
};

class NodeList : public Node
{
public:
  using ListType = std::vector<Node*>;

  NodeList() = default;
  ~NodeList() final = default;

  const Node* operator[](size_t index) const { return m_nodes[index]; }
  ListType::const_iterator begin() const { return m_nodes.begin(); }
  ListType::const_iterator end() const { return m_nodes.end(); }

  Node*& operator[](size_t index) { return m_nodes[index]; }
  ListType::iterator begin() { return m_nodes.begin(); }
  ListType::iterator end() { return m_nodes.end(); }

  const ListType& GetNodeList() const { return m_nodes; }
  const size_t GetNumChildren() const { return m_nodes.size(); }
  const Node* GetChild(size_t index) const;
  Node* GetChild(size_t index);

  bool HasChildren() const;
  const Node* GetFirst() const;
  Node* GetFirst();

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  // If node is null, does nothing.
  // If node is a NodeList, adds all the children from it (recursively).
  void AddNode(Node* node);

  // Prepends node/nodelist to this nodelist.
  void PrependNode(Node* node);

private:
  ListType m_nodes;
};

class TypeSpecifier : public Node
{
public:
  enum class TypeId
  {
    Error,
    Void,
    Boolean,
    Bit,
    Int,
    Float,
    APInt,
    Array,
    Struct
  };

public:
  TypeSpecifier(TypeId tid, const std::string& name, TypeSpecifier* base_type, unsigned num_bits);
  virtual ~TypeSpecifier() = default;

  TypeId GetTypeId() const { return m_type_id; }
  const std::string& GetName() const { return m_name; }
  const TypeSpecifier* GetBaseType() const { return m_base_type; }
  unsigned GetNumBits() const { return m_num_bits; }

  // Helper methods
  bool IsPrimitiveType() const { return (m_type_id >= TypeId::Void && m_type_id <= TypeId::APInt); }
  bool IsErrorType() const { return (m_type_id == TypeId::Error); }
  bool IsArrayType() const { return (m_type_id == TypeId::Array); }
  bool IsStructType() const { return (m_type_id == TypeId::Struct); }
  bool HasBooleanBase() const
  {
    return (m_type_id == TypeId::Boolean || (m_base_type && m_base_type->HasBooleanBase()));
  }
  bool HasBitBase() const { return (m_type_id == TypeId::Bit || (m_base_type && m_base_type->HasBitBase())); }
  bool HasIntBase() const { return (m_type_id == TypeId::Int || (m_base_type && m_base_type->HasIntBase())); }
  bool HasFloatBase() const { return (m_type_id == TypeId::Float || (m_base_type && m_base_type->HasFloatBase())); }
  bool IsVoid() const { return (m_type_id == TypeId::Void); }
  bool IsBoolean() const { return (m_type_id == TypeId::Boolean); }
  bool IsBit() const { return (m_type_id == TypeId::Bit); }
  bool IsInt() const { return (m_type_id == TypeId::Int); }
  bool IsFloat() const { return (m_type_id == TypeId::Float); }
  bool IsAPInt() const { return (m_type_id == TypeId::APInt); }
  bool IsIntOrAPInt() const { return (IsInt() || IsAPInt()); }

  // Validity -> error
  bool IsValid() const { return !IsErrorType(); }

  // Clones this type
  virtual TypeSpecifier* Clone() const;

  // Inherited methods
  virtual void Dump(ASTPrinter* printer) const override;
  virtual bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  virtual bool Accept(Visitor* visitor) override;

  // equality operators
  virtual bool operator==(const TypeSpecifier& rhs) const;
  bool operator!=(const TypeSpecifier& rhs) const;

protected:
  TypeId m_type_id;
  std::string m_name;
  TypeSpecifier* m_base_type;
  unsigned m_num_bits;
};

class ArrayTypeSpecifier : public TypeSpecifier
{
public:
  ArrayTypeSpecifier(const std::string& name, TypeSpecifier* base_type, Expression* dimensions);
  virtual ~ArrayTypeSpecifier() = default;

  Expression* GetArrayDimensions() const { return m_array_dimensions; }

  // Inherited methods
  virtual void Dump(ASTPrinter* printer) const override;
  virtual bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  virtual bool Accept(Visitor* visitor) override;

  // Clones this type
  virtual TypeSpecifier* Clone() const override;

  virtual bool operator==(const TypeSpecifier& rhs) const;

private:
  Expression* m_array_dimensions;
};

class Declaration : public Node
{
public:
  Declaration(const SourceLocation& sloc, TypeSpecifier* type, const std::string& name, bool constant);
  virtual ~Declaration() = default;

  const SourceLocation& GetSourceLocation() const { return m_sloc; }
  const TypeSpecifier* GetType() const { return m_type; }
  const std::string& GetName() const { return m_name; }
  bool IsConstant() const { return m_constant; }

protected:
  SourceLocation m_sloc;
  TypeSpecifier* m_type;
  std::string m_name;
  bool m_constant;
};

class Statement : public Node
{
public:
  Statement(const SourceLocation& sloc);
  virtual ~Statement() = default;

  const SourceLocation& GetSourceLocation() const { return m_sloc; }

protected:
  SourceLocation m_sloc;
};

class Expression : public Node
{
public:
  Expression(const SourceLocation& sloc);
  virtual ~Expression() = default;

  const SourceLocation& GetSourceLocation() const { return m_sloc; }

  virtual bool IsConstant() const;
  virtual bool GetConstantBool() const;
  virtual int GetConstantInt() const;
  virtual float GetConstantFloat() const;
  const TypeSpecifier* GetType() const;

protected:
  SourceLocation m_sloc;
  const TypeSpecifier* m_type = nullptr;
};

class ParameterDeclaration : public Declaration
{
public:
  ParameterDeclaration(const SourceLocation& sloc, TypeSpecifier* type_specifier, const std::string& name);
  ~ParameterDeclaration() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;
};
using ParameterDeclarationList = std::vector<ParameterDeclaration*>;

class StreamDeclaration : public Node
{
public:
  StreamDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                    TypeSpecifier* output_type_specifier, const char* name, ParameterDeclarationList* params);
  ~StreamDeclaration() = default;

  const TypeSpecifier* GetInputType() const { return m_input_type; }
  const TypeSpecifier* GetOutputType() const { return m_output_type; }
  ParameterDeclarationList* GetParameters() const { return m_parameters; }

  const SourceLocation& GetSourceLocation() const { return m_sloc; }
  const std::string& GetName() const { return m_name; }

protected:
  SourceLocation m_sloc;

  TypeSpecifier* m_input_type = nullptr;
  TypeSpecifier* m_output_type = nullptr;

  std::string m_name;

  ParameterDeclarationList* m_parameters;
};

class PipelineDeclaration : public StreamDeclaration
{
public:
  PipelineDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                      TypeSpecifier* output_type_specifier, const char* name, ParameterDeclarationList* params,
                      NodeList* statements);
  ~PipelineDeclaration() override;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  NodeList* GetStatements() const { return m_statements; }

private:
  NodeList* m_statements;
};

class SplitJoinDeclaration : public StreamDeclaration
{
public:
  SplitJoinDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                       TypeSpecifier* output_type_specifier, const char* name, ParameterDeclarationList* params,
                       NodeList* statements);
  ~SplitJoinDeclaration() override;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  NodeList* GetStatements() const { return m_statements; }

private:
  NodeList* m_statements;
};

class FilterDeclaration : public StreamDeclaration
{
public:
  FilterDeclaration(const SourceLocation& sloc, TypeSpecifier* input_type_specifier,
                    TypeSpecifier* output_type_specifier, const char* name, ParameterDeclarationList* params,
                    NodeList* vars, FilterWorkBlock* init, FilterWorkBlock* prework, FilterWorkBlock* work,
                    bool stateful);
  FilterDeclaration(TypeSpecifier* input_type_specifier, TypeSpecifier* output_type_specifier, const char* name,
                    ParameterDeclarationList* params, bool stateful, int peek_rate, int pop_rate, int push_rate);
  ~FilterDeclaration() = default;

  // TODO: Const here, but this is a larger change (e.g. visitor impact)
  FilterWorkBlock* GetInitBlock() const { return m_init; }
  FilterWorkBlock* GetPreworkBlock() const { return m_prework; }
  FilterWorkBlock* GetWorkBlock() const { return m_work; }
  ParameterDeclarationList* GetParameters() const { return m_parameters; }
  NodeList* GetStateVariables() const { return m_vars; }
  bool IsStateful() const { return m_stateful; }
  bool IsStateless() const { return !m_stateful; }

  bool HasInitBlock() const { return (m_init != nullptr); }
  bool HasPreworkBlock() const { return (m_prework != nullptr); }
  bool HasWorkBlock() const { return (m_work != nullptr); }
  bool HasStateVariables() const { return (m_vars != nullptr); }
  bool IsBuiltin() const { return m_builtin; }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  // Test for a builtin filter
  static FilterDeclaration* GetBuiltinFilter(ParserState* state, const std::string& name, NodeList* type_params,
                                             NodeList* params);
  static bool IsBuiltinFilter(const std::string& name);

private:
  NodeList* m_vars;
  FilterWorkBlock* m_init;
  FilterWorkBlock* m_prework;
  FilterWorkBlock* m_work;
  bool m_stateful;
  bool m_builtin;
};

struct FilterWorkParts
{
  NodeList* vars = nullptr;
  FilterWorkBlock* init = nullptr;
  FilterWorkBlock* prework = nullptr;
  FilterWorkBlock* work = nullptr;
};

class FilterWorkBlock final
{
public:
  FilterWorkBlock(const SourceLocation& sloc) : m_sloc(sloc) {}
  ~FilterWorkBlock() = default;

  void Dump(ASTPrinter* printer) const;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table);
  bool Accept(Visitor* visitor);

  Expression* GetPeekRateExpression() const { return m_peek_rate_expr; }
  Expression* GetPopRateExpression() const { return m_pop_rate_expr; }
  Expression* GetPushRateExpression() const { return m_push_rate_expr; }
  const NodeList* GetStatements() const { return m_stmts; }
  NodeList* GetStatements() { return m_stmts; }

  void SetPeekRateExpression(Expression* expr) { m_peek_rate_expr = expr; }
  void SetPopRateExpression(Expression* expr) { m_pop_rate_expr = expr; }
  void SetPushRateExpression(Expression* expr) { m_push_rate_expr = expr; }
  void SetStatements(NodeList* stmts) { m_stmts = stmts; }

private:
  SourceLocation m_sloc;
  Expression* m_peek_rate_expr = nullptr;
  Expression* m_pop_rate_expr = nullptr;
  Expression* m_push_rate_expr = nullptr;
  NodeList* m_stmts = nullptr;
};

// Function references map names -> types
class FunctionDeclaration : public Declaration
{
public:
  FunctionDeclaration(const std::string& name, TypeSpecifier* return_type,
                      const std::vector<TypeSpecifier*>& param_types);
  // FunctionDeclaration(const SourceLocation& sloc, const std::string& name, TypeSpecifier* return_type, NodeList*
  // params, NodeList* body);
  ~FunctionDeclaration() = default;

  TypeSpecifier* GetReturnType() const { return m_return_type; }
  const std::vector<TypeSpecifier*>& GetParameterTypes() const { return m_param_types; }

  // Turns println(int) into println___int.
  const std::string& GetSymbolName() const { return m_symbol_name; }

  // Adds streamit_ prefix to builtin symbols.
  std::string GetExecutableSymbolName() const;

  // Builtin function?
  bool IsBuiltin() const { return (m_body == nullptr); }

  void Dump(ASTPrinter* printer) const;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table);
  bool Accept(Visitor* visitor);

private:
  std::string m_symbol_name;
  TypeSpecifier* m_return_type;
  std::vector<TypeSpecifier*> m_param_types;
  NodeList* m_params;
  NodeList* m_body;
};

class IntegerLiteralExpression : public Expression
{
public:
  IntegerLiteralExpression(const SourceLocation& sloc, int value) : Expression(sloc), m_value(value) {}
  ~IntegerLiteralExpression() = default;

  bool IsConstant() const override { return true; }
  int GetConstantInt() const override { return m_value; }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  int GetValue() const { return m_value; }

private:
  int m_value;
};

class BooleanLiteralExpression : public Expression
{
public:
  BooleanLiteralExpression(const SourceLocation& sloc, bool value) : Expression(sloc), m_value(value) {}
  ~BooleanLiteralExpression() = default;

  bool IsConstant() const override { return true; }
  bool GetConstantBool() const override { return m_value; }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  bool GetValue() const { return m_value; }

private:
  bool m_value;
};

class FloatLiteralExpression : public Expression
{
public:
  FloatLiteralExpression(const SourceLocation& sloc, float value) : Expression(sloc), m_value(value) {}
  ~FloatLiteralExpression() = default;

  bool IsConstant() const override { return true; }
  float GetConstantFloat() const override { return m_value; }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  float GetValue() const { return m_value; }

private:
  float m_value;
};

class IdentifierExpression : public Expression
{
public:
  IdentifierExpression(const SourceLocation& sloc, const char* identifier);
  ~IdentifierExpression() = default;

  Declaration* GetReferencedDeclaration() const { return m_declaration; }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  std::string m_identifier;
  Declaration* m_declaration = nullptr;
};

class IndexExpression : public Expression
{
public:
  IndexExpression(const SourceLocation& sloc, Expression* array_expr, Expression* index_expr);
  ~IndexExpression() = default;

  Expression* GetArrayExpression() const;
  Expression* GetIndexExpression() const;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Expression* m_array_expression;
  Expression* m_index_expression;
};

class UnaryExpression : public Expression
{
public:
  enum Operator : unsigned int
  {
    PreIncrement,
    PreDecrement,
    PostIncrement,
    PostDecrement,
    Positive,
    Negative,
    LogicalNot,
    BitwiseNot
  };

  UnaryExpression(const SourceLocation& sloc, Operator op, Expression* rhs);
  ~UnaryExpression() = default;

  bool IsConstant() const override;
  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Operator GetOperator() const;
  Expression* GetRHSExpression() const;

private:
  Expression* m_rhs;
  Operator m_op;
};

class BinaryExpression : public Expression
{
public:
  enum Operator : unsigned int
  {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    LeftShift,
    RightShift
  };

  BinaryExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs);
  ~BinaryExpression() = default;

  bool IsConstant() const override;
  int GetConstantInt() const override;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetLHSExpression() const;
  Expression* GetRHSExpression() const;
  Operator GetOperator() const;

private:
  Expression* m_lhs;
  Expression* m_rhs;
  Operator m_op;
};

class RelationalExpression : public Expression
{
public:
  enum Operator : unsigned int
  {
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual
  };

  RelationalExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs);
  ~RelationalExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetLHSExpression() const;
  Expression* GetRHSExpression() const;
  const TypeSpecifier* GetIntermediateType() const;
  Operator GetOperator() const;

private:
  Expression* m_lhs;
  Expression* m_rhs;
  TypeSpecifier* m_intermediate_type;
  Operator m_op;
};

class LogicalExpression : public Expression
{
public:
  enum Operator : unsigned int
  {
    And,
    Or
  };

  LogicalExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs);
  ~LogicalExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetLHSExpression() const;
  Expression* GetRHSExpression() const;
  Operator GetOperator() const;

private:
  Expression* m_lhs;
  Expression* m_rhs;
  Operator m_op;
};

class CommaExpression : public Expression
{
public:
  CommaExpression(const SourceLocation& sloc, Expression* lhs, Expression* rhs);
  ~CommaExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetLHSExpression() const;
  Expression* GetRHSExpression() const;

private:
  Expression* m_lhs;
  Expression* m_rhs;
};

class AssignmentExpression : public Expression
{
public:
  enum Operator : unsigned int
  {
    Assign,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    LeftShift,
    RightShift
  };

  AssignmentExpression(const SourceLocation& sloc, Expression* lhs, Operator op, Expression* rhs);
  ~AssignmentExpression() = default;

  Expression* GetLValueExpression() const { return m_lhs; }
  Expression* GetInnerExpression() const { return m_rhs; }
  Operator GetOperator() const { return m_op; }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Expression* m_lhs;
  Expression* m_rhs;
  Operator m_op;
};

class PeekExpression : public Expression
{
public:
  PeekExpression(const SourceLocation& sloc, Expression* expr);
  ~PeekExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetIndexExpression() const;

private:
  Expression* m_expr;
};

class PopExpression : public Expression
{
public:
  PopExpression(const SourceLocation& sloc);
  ~PopExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;
};

class CallExpression : public Expression
{
public:
  CallExpression(const SourceLocation& sloc, const char* function_name, NodeList* args);
  ~CallExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  const std::string& GetFunctionName() const { return m_function_name; }
  const NodeList* GetArgList() const { return m_args; }
  bool HasArgs() const { return (m_args != nullptr); }

  const FunctionDeclaration* GetFunctionReference() const { return m_function_ref; }

private:
  std::string m_function_name;
  NodeList* m_args;

  const FunctionDeclaration* m_function_ref = nullptr;
};

class CastExpression : public Expression
{
public:
  CastExpression(const SourceLocation& sloc, TypeSpecifier* to_type, Expression* expr);
  ~CastExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  TypeSpecifier* GetToType() const { return m_to_type; }
  Expression* GetExpression() const { return m_expr; }

private:
  TypeSpecifier* m_to_type;
  Expression* m_expr;
};

class PushStatement : public Statement
{
public:
  PushStatement(const SourceLocation& sloc, Expression* expr);
  ~PushStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetValueExpression() const;

private:
  Expression* m_expr;
};

class AddStatement : public Statement
{
public:
  AddStatement(const SourceLocation& sloc, const char* filter_name, NodeList* type_parameters, NodeList* parameters);
  ~AddStatement();

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  const std::string& GetStreamName() const { return m_stream_name; }
  StreamDeclaration* GetStreamDeclaration() const { return m_stream_declaration; }
  NodeList* GetStreamParameters() const { return m_stream_parameters; }

private:
  std::string m_stream_name;
  NodeList* m_type_parameters;
  NodeList* m_stream_parameters;
  StreamDeclaration* m_stream_declaration = nullptr;
};

class SplitStatement : public Statement
{
public:
  enum Type : unsigned int
  {
    RoundRobin,
    Duplicate
  };

  SplitStatement(const SourceLocation& sloc, Type type, NodeList* distribution);
  ~SplitStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Type GetType() const { return m_type; }
  NodeList* GetDistribution() const { return m_distribution; }

private:
  Type m_type;
  NodeList* m_distribution;
};

class JoinStatement : public Statement
{
public:
  enum Type : unsigned int
  {
    RoundRobin
  };

  JoinStatement(const SourceLocation& sloc, Type type, NodeList* distribution);
  ~JoinStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Type GetType() const { return m_type; }
  NodeList* GetDistribution() const { return m_distribution; }

private:
  Type m_type;
  NodeList* m_distribution;
};

class InitializerListExpression : public Expression
{
public:
  InitializerListExpression(const SourceLocation& sloc);
  ~InitializerListExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;
  bool IsConstant() const override;

  void AddExpression(Expression* expr);
  const std::vector<Expression*>& GetExpressionList() const;
  size_t GetListSize() const;

private:
  std::vector<Expression*> m_expressions;
};

struct InitDeclarator
{
  SourceLocation sloc;
  const char* name;
  Expression* initializer;
};
using InitDeclaratorList = std::vector<InitDeclarator>;

class VariableDeclaration final : public Declaration
{
public:
  VariableDeclaration(const SourceLocation& sloc, TypeSpecifier* type_specifier, const char* name,
                      Expression* initializer);
  ~VariableDeclaration() override final = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  bool HasInitializer() const { return (m_initializer != nullptr); }
  Expression* GetInitializer() const { return m_initializer; }
  void RemoveInitializer() { m_initializer = nullptr; }

  static Node* CreateDeclarations(TypeSpecifier* type_specifier, const InitDeclaratorList* declarator_list);

private:
  Expression* m_initializer;
};

class ExpressionStatement : public Statement
{
public:
  ExpressionStatement(const SourceLocation& sloc, Expression* expr);
  ~ExpressionStatement() = default;

  Expression* GetInnerExpression() const;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Expression* m_expr;
};

class IfStatement : public Statement
{
public:
  IfStatement(const SourceLocation& sloc, Expression* expr, Node* then_stmts, Node* else_stmts);
  ~IfStatement() = default;

  Expression* GetInnerExpression() const;
  Node* GetThenStatements() const;
  Node* GetElseStatements() const;
  bool HasElseStatements() const;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Expression* m_expr;
  Node* m_then;
  Node* m_else;
};

class ForStatement : public Statement
{
public:
  ForStatement(const SourceLocation& sloc, Node* init, Expression* cond, Expression* loop, Node* inner);
  ~ForStatement() = default;

  Node* GetInitStatements() const;
  Expression* GetConditionExpression() const;
  Expression* GetLoopExpression() const;
  Node* GetInnerStatements() const;
  bool HasInitStatements() const;
  bool HasConditionExpression() const;
  bool HasLoopExpression() const;
  bool HasInnerStatements() const;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Node* m_init;
  Expression* m_cond;
  Expression* m_loop;
  Node* m_inner;
};

class BreakStatement : public Statement
{
public:
  BreakStatement(const SourceLocation& sloc);
  ~BreakStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;
};

class ContinueStatement : public Statement
{
public:
  ContinueStatement(const SourceLocation& sloc);
  ~ContinueStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;
};

class ReturnStatement : public Statement
{
public:
  ReturnStatement(const SourceLocation& sloc, Expression* expr = nullptr);
  ~ReturnStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  Expression* GetInnerExpression() const;
  bool HasReturnValue() const;

private:
  Expression* m_expr;
};
}
