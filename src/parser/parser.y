%{
#include <cassert>
#include "parser/scanner.h"
#include "parser/parser_state.h"
#include "parser/ast.h"
#include "parser/type.h"

using namespace AST;

#define YYDEBUG 1
%}

%locations
%error-verbose
%parse-param {struct ParserState* state}

%token TK_ARROW
%token TK_FILTER TK_PIPELINE
%token TK_PEEK TK_POP TK_PUSH
%token TK_ADD TK_LOOP TK_ENQUEUE
%token TK_SPLIT TK_DUPLICATE
%token TK_JOIN TK_ROUNDROBIN
%token TK_INIT TK_PREWORK TK_WORK

%token TK_BOOLEAN TK_BIT TK_INT TK_FLOAT TK_COMPLEX

%token TK_LOGICAL_AND TK_LOGICAL_OR
%token TK_EQUALS TK_NOT_EQUALS
%token TK_INCREMENT TK_DECREMENT TK_LSHIFT TK_RSHIFT
%token TK_LTE TK_GTE

%token TK_IF TK_ELSE TK_FOR TK_DO TK_WHILE TK_CONTINUE TK_BREAK TK_RETURN

%token TK_BOOLEAN_LITERAL TK_INTEGER_LITERAL TK_FLOAT_LITERAL TK_STRING_LITERAL TK_IDENTIFIER

%start		Program

%union {
  const Type* type;

  AST::Node* node;
  AST::NodeList* node_list;
  AST::Program* program;
  AST::Declaration* decl;
  AST::Statement* stmt;
  AST::Expression* expr;
  AST::PipelineDeclaration* pipeline_decl;
  AST::FilterDeclaration* filter_decl;
  AST::FilterWorkParts* filter_work_parts;
  AST::FilterWorkBlock* filter_work_block;
  AST::StringList* string_list;

  AST::InitDeclarator init_declarator;
  AST::InitDeclaratorList *init_declarator_list;

  char* identifier;

  bool boolean_literal;
  int integer_literal;
  double float_literal;
  char* string_literal;
}

%type <program> Program
%type <pipeline_decl> PipelineDeclaration
%type <node> PipelineStatement
%type <node_list> PipelineStatementList
%type <filter_decl> FilterDeclaration
%type <filter_work_parts> FilterDefinition
%type <filter_work_parts> FilterWorkParts
%type <filter_work_block> FilterWorkBlock
%type <node_list> StatementList
%type <node> StatementListItem
%type <node> CompoundStatement
%type <node> Declaration
%type <type> DeclarationSpecifiers
%type <init_declarator> InitDeclarator
%type <init_declarator_list> InitDeclaratorList
%type <identifier> Declarator
%type <expr> Initializer
/*%type <string_list> IdentifierList*/
%type <stmt> Statement
%type <stmt> ExpressionStatement
%type <stmt> SelectionStatement
%type <stmt> IterationStatement
%type <stmt> JumpStatement
%type <expr> Expression
%type <expr> Expression_opt
%type <expr> PrimaryExpression
%type <expr> PostfixExpression
%type <expr> UnaryExpression
%type <expr> CastExpression
%type <expr> MultiplicativeExpression
%type <expr> AdditiveExpression
%type <expr> ShiftExpression
%type <expr> RelationalExpression
%type <expr> EqualityExpression
%type <expr> BitwiseAndExpression
%type <expr> BitwiseOrExpression
%type <expr> BitwiseXorExpression
%type <expr> LogicalAndExpression
%type <expr> LogicalOrExpression
%type <expr> ConditionalExpression
%type <expr> AssignmentExpression
%type <identifier> Identifier TK_IDENTIFIER
%type <integer_literal> IntegerLiteral TK_INTEGER_LITERAL
%type <boolean_literal> BooleanLiteral TK_BOOLEAN_LITERAL
%type <type> Type

/* Tie the else branch of an if to the outer-most if */
%nonassoc IF_THEN
%nonassoc TK_ELSE

%%

empty : ;
Identifier : TK_IDENTIFIER ;
IntegerLiteral : TK_INTEGER_LITERAL ;
BooleanLiteral : TK_BOOLEAN_LITERAL ;

Program
  : PipelineDeclaration { $$ = state->program = new Program(); state->program->AddPipeline($1); }
  | FilterDeclaration { $$ = state->program = new Program(); state->program->AddFilter($1); }
  | Program PipelineDeclaration { $1->AddPipeline($2); }
  | Program FilterDeclaration { $1->AddFilter($2); }
  ;

Type
  : TK_INT { $$ = Type::GetIntType(); }
  | TK_BOOLEAN { $$ = Type::GetBooleanType(); }
  ;

PipelineDeclaration
  : Type TK_ARROW Type TK_PIPELINE Identifier '{' PipelineStatementList '}' { $$ = new PipelineDeclaration($1, $3, $5, $7); }
  ;

PipelineStatement
  : TK_ADD Identifier '(' ')' ';' { $$ = new PipelineAddStatement($2, nullptr); }
  ;

PipelineStatementList
  : PipelineStatement { $$ = new NodeList(); $$->AddNode($1); }
  | PipelineStatementList PipelineStatement { $1->AddNode($2); }
  ;

FilterDeclaration
  : Type TK_ARROW Type TK_FILTER Identifier FilterDefinition { $$ = new FilterDeclaration($1, $3, $5, $6->vars, $6->init, $6->prework, $6->work); }
  ;

FilterDefinition
  : '{' FilterWorkParts '}' { $$ = $2; }
  | '{' '}' { $$ = new FilterWorkParts(); }
  ;

/* TODO: Raise error on duplicate definition */
FilterWorkParts
  : TK_INIT FilterWorkBlock { $$ = new FilterWorkParts(); $$->init = $2; }
  | TK_PREWORK FilterWorkBlock { $$ = new FilterWorkParts(); $$->prework = $2; }
  | TK_WORK FilterWorkBlock { $$ = new FilterWorkParts(); $$->work = $2; }
  | Declaration { $$ = new FilterWorkParts(); $$->vars = new NodeList(); $$->vars->AddNode($1); }
  | FilterWorkParts TK_INIT FilterWorkBlock { assert(!$1->init); $1->init = $3; }
  | FilterWorkParts TK_PREWORK FilterWorkBlock { assert(!$1->prework); $1->prework = $3; }
  | FilterWorkParts TK_WORK FilterWorkBlock { assert(!$1->work); $1->work = $3; }
  | FilterWorkParts Declaration { if (!$$->vars) { $$->vars = new NodeList(); } $$->vars->AddNode($2); }
  ;

/* TODO: Raise error on negative peek/push/pop */
FilterWorkBlock
  : TK_PEEK IntegerLiteral { $$ = new FilterWorkBlock(); $$->SetPeekRate($2); }
  | TK_POP IntegerLiteral { $$ = new FilterWorkBlock(); $$->SetPopRate($2); }
  | TK_PUSH IntegerLiteral { $$ = new FilterWorkBlock(); $$->SetPushRate($2); }
  | '{' StatementList '}' { $$ = new FilterWorkBlock(); $$->SetStatements($2); }
  | FilterWorkBlock TK_PEEK IntegerLiteral { $$->SetPeekRate($3); }
  | FilterWorkBlock TK_POP IntegerLiteral { $$->SetPopRate($3); }
  | FilterWorkBlock TK_PUSH IntegerLiteral { $$->SetPushRate($3); }
  | FilterWorkBlock '{' StatementList '}' { $$->SetStatements($3); }
  ;

StatementList
  : StatementListItem { $$ = new NodeList(); $$->AddNode($1); }
  | StatementList StatementListItem { $1->AddNode($2); }
  ;

/* TODO: Rework this */
DeclarationSpecifiers
  : Type
  ;

Declarator
  : Identifier
  ;

InitDeclarator
  : Declarator '=' Initializer { $$.name = $1; $$.initializer = $3; }
  ;

InitDeclaratorList
  : InitDeclarator { $$ = new InitDeclaratorList(); $$->push_back($1); }
  | InitDeclaratorList ',' InitDeclarator { $1->push_back($3); }
  ;

Declaration
  : DeclarationSpecifiers ';' { $$ = nullptr; }   /* TODO: Is this needed? */
  | DeclarationSpecifiers InitDeclaratorList ';' { $$ = VariableDeclaration::CreateDeclarations($1, $2); /*delete $2;*/ }
  ;

/* TODO: Arrays here */
Initializer
  : AssignmentExpression
  ;

StatementListItem
  : Declaration { $$ = $1; }
  | CompoundStatement { $$ = $1; }
  | Statement { $$ = $1; }
  ;

/* TODO: Scope for variable names. */
/* Currently, it'll eventually get AddNode()'d, which will insert all the children */
CompoundStatement
  : '{' '}' { $$ = nullptr; }
  | '{' StatementList '}' { $$ = $2; }
  ;

Statement
  : ExpressionStatement { $$ = $1; }
  | SelectionStatement { $$ = $1; }
  | IterationStatement { $$ = $1; }
  | JumpStatement { $$ = $1; }
  ;
  
ExpressionStatement
  : Expression ';' { $$ = new ExpressionStatement($1); }
  | ';' { $$ = nullptr; }
  ;

SelectionStatement
  : TK_IF '(' Expression ')' StatementListItem %prec IF_THEN { $$ = new IfStatement($3, $5, nullptr); }
  | TK_IF '(' Expression ')' StatementListItem TK_ELSE StatementListItem { $$ = new IfStatement($3, $5, $7); }
  ;

IterationStatement
  : TK_FOR '(' StatementListItem Expression_opt ';' ')' StatementListItem { $$ = new ForStatement($3, $4, nullptr, $7); }
  | TK_FOR '(' StatementListItem Expression_opt ';' Expression ')' StatementListItem { $$ = new ForStatement($3, $4, $6, $8); }
  ;

JumpStatement
  : TK_BREAK ';' { $$ = new BreakStatement(); }
  | TK_CONTINUE ';' { $$ = new ContinueStatement(); }
  | TK_RETURN ';' { $$ = new ReturnStatement(); }
  ;

Expression
  : AssignmentExpression { $$ = $1; }
  | Expression ',' AssignmentExpression { $$ = new CommaExpression($1, $3); }
  ;

Expression_opt
  : Expression { $$ = $1; }
  | empty { $$ = nullptr; }
  ;

PrimaryExpression
  : Identifier { $$ = new IdentifierExpression($1); }
  | IntegerLiteral { $$ = new IntegerLiteralExpression($1); }
  | BooleanLiteral { $$ = new BooleanLiteralExpression($1); }
  | '(' Expression ')' { $$ = $2; }
  ;

PostfixExpression
  : PrimaryExpression { $$ = $1; }
  /*| PostfixExpression '[' Expression ']'*/
  /*| PostfixExpression '(' ')'*/
  /*| PostfixExpression '(' ArgumentExpressionList ')'*/
  /*| PostfixExpression TK_INCREMENT*/
  /*| PostfixExpression TK_DECREMENT*/
  | TK_PEEK '(' Expression ')' { $$ = new PeekExpression($3); }
  | TK_POP '(' ')' { $$ = new PopExpression(); }
  | TK_PUSH '(' Expression ')' { $$ = new PushExpression($3); }
  ;

UnaryExpression
  : PostfixExpression { $$ = $1; }
  /*| '+' UnaryExpression { $$ = new UnaryExpression($2, UnaryExpression::Positive); }*/
  /*| '-' UnaryExpression { $$ = new UnaryExpression($2, UnaryExpression::Negative); }*/
  /*| '!' UnaryExpression { $$ = new UnaryExpression($2, UnaryExpression::LogicalNot); }*/
  /*| '~' UnaryExpression { $$ = new UnaryExpression($2, UnaryExpression::BitwiseNot); }*/
  ;

CastExpression
  : UnaryExpression { $$ = $1; }
  /*| '(' Type ')' CastExpression*/
  ;

MultiplicativeExpression
  : CastExpression { $$ = $1; }
  | MultiplicativeExpression '*' CastExpression { $$ = new BinaryExpression($1, BinaryExpression::Multiply, $3); }
  | MultiplicativeExpression '/' CastExpression { $$ = new BinaryExpression($1, BinaryExpression::Divide, $3); }
  | MultiplicativeExpression '%' CastExpression { $$ = new BinaryExpression($1, BinaryExpression::Modulo, $3); }
  ;

AdditiveExpression
  : MultiplicativeExpression { $$ = $1; }
  | AdditiveExpression '+' MultiplicativeExpression { $$ = new BinaryExpression($1, BinaryExpression::Add, $3); }
  | AdditiveExpression '-' MultiplicativeExpression { $$ = new BinaryExpression($1, BinaryExpression::Subtract, $3); }
  ;

ShiftExpression
  : AdditiveExpression { $$ = $1; }
  | ShiftExpression TK_LSHIFT AdditiveExpression { $$ = new BinaryExpression($1, BinaryExpression::LeftShift, $3); }
  | ShiftExpression TK_RSHIFT AdditiveExpression { $$ = new BinaryExpression($1, BinaryExpression::RightShift, $3); }
  ;

RelationalExpression
  : ShiftExpression { $$ = $1; }
  | RelationalExpression '<' ShiftExpression { $$ = new RelationalExpression($1, RelationalExpression::Less, $3); }
  | RelationalExpression '>' ShiftExpression { $$ = new RelationalExpression($1, RelationalExpression::Greater, $3); }
  | RelationalExpression TK_LTE ShiftExpression { $$ = new RelationalExpression($1, RelationalExpression::LessEqual, $3); }
  | RelationalExpression TK_GTE ShiftExpression { $$ = new RelationalExpression($1, RelationalExpression::GreaterEqual, $3); }
  ;

EqualityExpression
  : RelationalExpression { $$ = $1; }
  | EqualityExpression TK_EQUALS RelationalExpression { $$ = new RelationalExpression($1, RelationalExpression::Equal, $3); }
  | EqualityExpression TK_NOT_EQUALS RelationalExpression { $$ = new RelationalExpression($1, RelationalExpression::NotEqual, $3); }
  ;

BitwiseAndExpression
  : EqualityExpression
  | BitwiseAndExpression '&' EqualityExpression { $$ = new BinaryExpression($1, BinaryExpression::BitwiseAnd, $3); }
  ;

BitwiseXorExpression
  : BitwiseAndExpression
  | BitwiseXorExpression '^' BitwiseAndExpression { $$ = new BinaryExpression($1, BinaryExpression::BitwiseXor, $3); }
  ;

BitwiseOrExpression
  : BitwiseXorExpression
  | BitwiseOrExpression '|' BitwiseXorExpression { $$ = new BinaryExpression($1, BinaryExpression::BitwiseOr, $3); }
  ;

LogicalAndExpression
  : BitwiseOrExpression
  | LogicalAndExpression TK_LOGICAL_AND BitwiseOrExpression { $$ = new LogicalExpression($1, LogicalExpression::And, $3); }
  ;

LogicalOrExpression
  : LogicalAndExpression
  | LogicalOrExpression TK_LOGICAL_OR LogicalAndExpression { $$ = new LogicalExpression($1, LogicalExpression::Or, $3); }
  ;

ConditionalExpression
  : LogicalOrExpression
  /*| LogicalOrExpression '?' Expression ':' ConditionalExpression { $$ = new ConditionalExpression($1, $3, $5); }*/
  ;

AssignmentExpression
  : ConditionalExpression
  | UnaryExpression '=' AssignmentExpression { $$ = new AssignmentExpression($1, $3); }
  ;

%%

