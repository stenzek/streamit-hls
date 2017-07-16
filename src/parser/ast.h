#pragma once
#include <list>
#include <string>
#include <vector>
#include "parser/symbol_table.h"

class ASTPrinter;
class CodeGenerator;
class ParserState;
class Type;

namespace AST
{
class Visitor;
class Program;
class Node;
class NodeList;
class Statement;
class Declaration;
class Expression;
class PipelineDeclaration;
class PipelineAddStatement;
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

  const std::string& operator[](size_t index) const
  {
    return m_values[index];
  }
  ListType::const_iterator begin() const
  {
    return m_values.begin();
  }
  ListType::const_iterator end() const
  {
    return m_values.end();
  }

  std::string& operator[](size_t index)
  {
    return m_values[index];
  }
  ListType::iterator begin()
  {
    return m_values.begin();
  }
  ListType::iterator end()
  {
    return m_values.end();
  }

  void AddString(const char* str)
  {
    m_values.emplace_back(str);
  }
  void AddString(const std::string& str)
  {
    m_values.push_back(str);
  }

private:
  ListType m_values;
};

class Program
{
public:
  Program() = default;
  ~Program() = default;

  const std::list<FilterDeclaration*>& GetFilterList() const;

  void Dump(ASTPrinter* printer) const;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table);
  bool Accept(Visitor* visitor);

  void AddPipeline(PipelineDeclaration* decl);
  void AddFilter(FilterDeclaration* decl);

private:
  std::list<PipelineDeclaration*> m_pipelines;
  std::list<FilterDeclaration*> m_filters;
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

  const Node* operator[](size_t index) const
  {
    return m_nodes[index];
  }
  ListType::const_iterator begin() const
  {
    return m_nodes.begin();
  }
  ListType::const_iterator end() const
  {
    return m_nodes.end();
  }

  Node*& operator[](size_t index)
  {
    return m_nodes[index];
  }
  ListType::iterator begin()
  {
    return m_nodes.begin();
  }
  ListType::iterator end()
  {
    return m_nodes.end();
  }

  const ListType& GetNodeList() const
  {
    return m_nodes;
  }

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

class Declaration : public Node
{
public:
  Declaration(const SourceLocation& sloc);
  virtual ~Declaration() = default;

  const SourceLocation& GetSourceLocation() const;

protected:
  SourceLocation m_sloc;
};

class Statement : public Node
{
public:
  Statement(const SourceLocation& sloc);
  virtual ~Statement() = default;

  const SourceLocation& GetSourceLocation() const;

protected:
  SourceLocation m_sloc;
};

class Expression : public Node
{
public:
  Expression(const SourceLocation& sloc);
  virtual ~Expression() = default;

  const SourceLocation& GetSourceLocation() const;

  virtual bool IsConstant() const;
  const Type* GetType() const;

protected:
  SourceLocation m_sloc;
  const Type* m_type = nullptr;
};

// References are placed in the symbol table to resolve names -> type pointers
class TypeReference : public Node
{
public:
  TypeReference(const std::string& name, const Type* type);
  ~TypeReference() = default;

  const std::string& GetName() const
  {
    return m_name;
  }
  const Type* GetType() const
  {
    return m_type;
  }

  void Dump(ASTPrinter* printer) const override
  {
  }
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override
  {
    return true;
  }
  bool Accept(Visitor* visitor) override
  {
    return false;
  }

private:
  std::string m_name;
  const Type* m_type;
};

class TypeName : public Node
{
public:
  TypeName(const SourceLocation& sloc);
  ~TypeName() = default;

  const std::string& GetBaseTypeName() const;
  const std::vector<int>& GetArraySizes() const;
  const Type* GetFinalType() const;

  void SetBaseTypeName(const char* name);
  void AddArraySize(int size);

  void Merge(ParserState* state, TypeName* rhs);

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  SourceLocation m_sloc;
  std::string m_base_type_name;
  std::vector<int> m_array_sizes;
  const Type* m_final_type = nullptr;
};

class StructSpecifier : public Node
{
public:
  StructSpecifier(const SourceLocation& sloc, const char* name);
  ~StructSpecifier() = default;

  const std::string& GetName() const;
  const std::vector<std::pair<std::string, TypeName*>>& GetFields() const;

  void AddField(const char* name, TypeName* specifier);

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  SourceLocation m_sloc;
  std::string m_name;
  std::vector<std::pair<std::string, TypeName*>> m_fields;
  const Type* m_final_type = nullptr;
};

class PipelineDeclaration : public Declaration
{
public:
  PipelineDeclaration(const SourceLocation& sloc, TypeName* input_type_specifier, TypeName* output_type_specifier,
                      const char* name, NodeList* statements);
  ~PipelineDeclaration() override;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  TypeName* m_input_type_specifier;
  TypeName* m_output_type_specifier;
  const Type* m_input_type = nullptr;
  const Type* m_output_type = nullptr;
  std::string m_name;
  NodeList* m_statements;
};

class PipelineAddStatement : public Statement
{
public:
  PipelineAddStatement(const SourceLocation& sloc, const char* filter_name, const NodeList* parameters);
  ~PipelineAddStatement();

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  std::string m_filter_name;
  const NodeList* m_filter_parameters;
  Node* m_filter_declaration = nullptr;
};

class FilterDeclaration : public Declaration
{
public:
  FilterDeclaration(const SourceLocation& sloc, TypeName* input_type_specifier, TypeName* output_type_specifier,
                    const char* name, NodeList* vars, FilterWorkBlock* init, FilterWorkBlock* prework,
                    FilterWorkBlock* work);
  ~FilterDeclaration() = default;

  const Type* GetInputType() const
  {
    return m_input_type;
  }
  const Type* GetOutputType() const
  {
    return m_output_type;
  }

  const std::string& GetName() const
  {
    return m_name;
  }

  // TODO: Const here, but this is a larger change (e.g. visitor impact)
  FilterWorkBlock* GetInitBlock() const
  {
    return m_init;
  }
  FilterWorkBlock* GetPreworkBlock() const
  {
    return m_prework;
  }
  FilterWorkBlock* GetWorkBlock() const
  {
    return m_work;
  }
  NodeList* GetStateVariables() const
  {
    return m_vars;
  }

  bool HasInitBlock() const
  {
    return (m_init != nullptr);
  }
  bool HasPreworkBlock() const
  {
    return (m_prework != nullptr);
  }
  bool HasWorkBlock() const
  {
    return (m_work != nullptr);
  }
  bool HasStateVariables() const
  {
    return (m_vars != nullptr);
  }

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  // Moves non-constant state initializers to the init function
  void MoveStateAssignmentsToInit();

  TypeName* m_input_type_specifier;
  TypeName* m_output_type_specifier;
  const Type* m_input_type = nullptr;
  const Type* m_output_type = nullptr;
  std::string m_name;
  NodeList* m_vars;
  FilterWorkBlock* m_init;
  FilterWorkBlock* m_prework;
  FilterWorkBlock* m_work;
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
  FilterWorkBlock() = default;
  ~FilterWorkBlock() = default;

  void Dump(ASTPrinter* printer) const;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table);
  bool Accept(Visitor* visitor);

  int GetPeekRate() const
  {
    return m_peek_rate;
  }
  int GetPopRate() const
  {
    return m_pop_rate;
  }
  int GetPushRate() const
  {
    return m_push_rate;
  }
  const NodeList* GetStatements() const
  {
    return m_stmts;
  }
  NodeList* GetStatements()
  {
    return m_stmts;
  }

  void SetPeekRate(int rate)
  {
    m_peek_rate = rate;
  }
  void SetPopRate(int rate)
  {
    m_pop_rate = rate;
  }
  void SetPushRate(int rate)
  {
    m_push_rate = rate;
  }
  void SetStatements(NodeList* stmts)
  {
    m_stmts = stmts;
  }

private:
  int m_peek_rate = -1;
  int m_pop_rate = -1;
  int m_push_rate = -1;
  NodeList* m_stmts = nullptr;
};

class IntegerLiteralExpression : public Expression
{
public:
  IntegerLiteralExpression(const SourceLocation& sloc, int value);
  ~IntegerLiteralExpression() = default;

  bool IsConstant() const override;
  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  int GetValue() const;

private:
  int m_value;
};

class BooleanLiteralExpression : public Expression
{
public:
  BooleanLiteralExpression(const SourceLocation& sloc, bool value);
  ~BooleanLiteralExpression() = default;

  bool IsConstant() const override;
  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  bool GetValue() const;

private:
  bool m_value;
};

class IdentifierExpression : public Expression
{
public:
  IdentifierExpression(const SourceLocation& sloc, const char* identifier);
  ~IdentifierExpression() = default;

  VariableDeclaration* GetReferencedVariable() const;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  std::string m_identifier;
  VariableDeclaration* m_identifier_declaration = nullptr;
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
  const Type* GetIntermediateType() const;
  Operator GetOperator() const;

private:
  Expression* m_lhs;
  Expression* m_rhs;
  const Type* m_intermediate_type;
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
  AssignmentExpression(const SourceLocation& sloc, Expression* lhs, Expression* rhs);
  ~AssignmentExpression() = default;

  Expression* GetLValueExpression() const;
  Expression* GetInnerExpression() const;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Expression* m_lhs;
  Expression* m_rhs;
};

class PeekExpression : public Expression
{
public:
  PeekExpression(const SourceLocation& sloc, Expression* expr);
  ~PeekExpression() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

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

class PushStatement : public Statement
{
public:
  PushStatement(const SourceLocation& sloc, Expression* expr);
  ~PushStatement() = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

private:
  Expression* m_expr;
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
  VariableDeclaration(const SourceLocation& sloc, TypeName* type_specifier, const char* name, Expression* initializer);
  ~VariableDeclaration() override final = default;

  void Dump(ASTPrinter* printer) const override;
  bool SemanticAnalysis(ParserState* state, LexicalScope* symbol_table) override;
  bool Accept(Visitor* visitor) override;

  const Type* GetType() const
  {
    return m_type;
  }
  const std::string& GetName() const
  {
    return m_name;
  }
  bool HasInitializer() const
  {
    return (m_initializer != nullptr);
  }
  Expression* GetInitializer() const
  {
    return m_initializer;
  }
  void RemoveInitializer()
  {
    m_initializer = nullptr;
  }

  static Node* CreateDeclarations(TypeName* type_specifier, const InitDeclaratorList* declarator_list);

private:
  TypeName* m_type_specifier;
  const Type* m_type = nullptr;
  std::string m_name;
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
