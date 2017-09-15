#pragma once

namespace AST
{
class Visitor;
class Node;
class NodeList;
class Statement;
class Declaration;
class Expression;
class TypeReference;
class TypeName;
class StructSpecifier;
class ParameterDeclaration;
class StreamDeclaration;
class PipelineDeclaration;
class SplitJoinDeclaration;
class FunctionDeclaration;
class AddStatement;
class SplitStatement;
class JoinStatement;
class FilterDeclaration;
class FilterWorkBlock;
class IntegerLiteralExpression;
class BooleanLiteralExpression;
class IdentifierExpression;
class IndexExpression;
class UnaryExpression;
class BinaryExpression;
class RelationalExpression;
class LogicalExpression;
class CommaExpression;
class AssignmentExpression;
class PeekExpression;
class PopExpression;
class CallExpression;
class CastExpression;
class PushStatement;
class InitializerListExpression;
class VariableDeclaration;
class ExpressionStatement;
class IfStatement;
class ForStatement;
class BreakStatement;
class ContinueStatement;
class ReturnStatement;

class Visitor
{
public:
  virtual bool Visit(Node* node);
  virtual bool Visit(Statement* node);
  virtual bool Visit(Declaration* node);
  virtual bool Visit(Expression* node);
  virtual bool Visit(TypeReference* node);
  virtual bool Visit(TypeName* node);
  virtual bool Visit(StructSpecifier* node);
  virtual bool Visit(ParameterDeclaration* node);
  virtual bool Visit(StreamDeclaration* node);
  virtual bool Visit(PipelineDeclaration* node);
  virtual bool Visit(SplitJoinDeclaration* node);
  virtual bool Visit(FunctionDeclaration* node);
  virtual bool Visit(AddStatement* node);
  virtual bool Visit(SplitStatement* node);
  virtual bool Visit(JoinStatement* node);
  virtual bool Visit(FilterDeclaration* node);
  virtual bool Visit(FilterWorkBlock* node);
  virtual bool Visit(BooleanLiteralExpression* node);
  virtual bool Visit(IntegerLiteralExpression* node);
  virtual bool Visit(IdentifierExpression* node);
  virtual bool Visit(IndexExpression* node);
  virtual bool Visit(UnaryExpression* node);
  virtual bool Visit(BinaryExpression* node);
  virtual bool Visit(RelationalExpression* node);
  virtual bool Visit(LogicalExpression* node);
  virtual bool Visit(CommaExpression* node);
  virtual bool Visit(AssignmentExpression* node);
  virtual bool Visit(PeekExpression* node);
  virtual bool Visit(PopExpression* node);
  virtual bool Visit(CallExpression* node);
  virtual bool Visit(CastExpression* node);
  virtual bool Visit(PushStatement* node);
  virtual bool Visit(InitializerListExpression* node);
  virtual bool Visit(VariableDeclaration* node);
  virtual bool Visit(ExpressionStatement* node);
  virtual bool Visit(IfStatement* node);
  virtual bool Visit(ForStatement* node);
  virtual bool Visit(BreakStatement* node);
  virtual bool Visit(ContinueStatement* node);
  virtual bool Visit(ReturnStatement* node);
};
}