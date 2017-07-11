#pragma once

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
class PushExpression;
class VariableDeclaration;
class ExpressionStatement;

class Visitor
{
public:
  virtual bool Visit(Program* node);
  virtual bool Visit(Node* node);
  virtual bool Visit(Statement* node);
  virtual bool Visit(Declaration* node);
  virtual bool Visit(Expression* node);
  virtual bool Visit(PipelineDeclaration* node);
  virtual bool Visit(PipelineAddStatement* node);
  virtual bool Visit(FilterDeclaration* node);
  virtual bool Visit(FilterWorkBlock* node);
  virtual bool Visit(IntegerLiteralExpression* node);
  virtual bool Visit(IdentifierExpression* node);
  virtual bool Visit(BinaryExpression* node);
  virtual bool Visit(AssignmentExpression* node);
  virtual bool Visit(PeekExpression* node);
  virtual bool Visit(PopExpression* node);
  virtual bool Visit(PushExpression* node);
  virtual bool Visit(VariableDeclaration* node);
  virtual bool Visit(ExpressionStatement* node);
};
}