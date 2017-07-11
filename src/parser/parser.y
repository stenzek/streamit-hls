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
%token TK_INCREMENT TK_DECREMENT
%token TK_LTE TK_GTE

%token TK_IF TK_ELSE TK_FOR TK_DO TK_WHILE TK_CONTINUE TK_BREAK

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
%type <node_list> VariableDeclarationList
%type <node_list> VariableDeclarationListPart
/*%type <string_list> IdentifierList*/
%type <stmt> Statement
%type <expr> Expression
%type <identifier> Identifier TK_IDENTIFIER
%type <integer_literal> IntegerLiteral TK_INTEGER_LITERAL
%type <type> Type

%left '+'
%left '-'
%left '*'
%left '/'
%left '='

%%

/*empty : ;*/
Identifier : TK_IDENTIFIER ;
IntegerLiteral : TK_INTEGER_LITERAL ;

Program
  : PipelineDeclaration { $$ = state->program = new Program(); state->program->AddPipeline($1); }
  | FilterDeclaration { $$ = state->program = new Program(); state->program->AddFilter($1); }
  | Program PipelineDeclaration { $1->AddPipeline($2); }
  | Program FilterDeclaration { $1->AddFilter($2); }
  ;

Type
  : TK_INT { $$ = Type::GetIntType(); }
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
  | VariableDeclarationList { $$ = new FilterWorkParts(); $$->vars = $1; }
  | FilterWorkParts TK_INIT FilterWorkBlock { assert(!$1->init); $1->init = $3; }
  | FilterWorkParts TK_PREWORK FilterWorkBlock { assert(!$1->prework); $1->prework = $3; }
  | FilterWorkParts TK_WORK FilterWorkBlock { assert(!$1->work); $1->work = $3; }
  | FilterWorkParts VariableDeclarationList { if (!$$->vars) { $$->vars = $2; } else { $$->vars->AddNodes($2); } }
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
  : VariableDeclarationList { $$ = new NodeList(); $$->AddNodes($1); }
  | Statement { $$ = new NodeList(); $$->AddNode($1); }
  | StatementList VariableDeclarationList { $1->AddNodes($2); }
  | StatementList Statement { $1->AddNode($2); }
  ;

VariableDeclarationList
  : VariableDeclarationListPart ';' { $$ = $1; }
  ;

VariableDeclarationListPart
  : Type Identifier
  {
    $$ = new NodeList();
    $$->AddNode(new VariableDeclaration($1, $2, nullptr));
  }
  | Type Identifier '=' Expression
  {
    $$ = new NodeList();
    $$->AddNode(new VariableDeclaration($1, $2, $4));
  }
  | VariableDeclarationListPart ',' Identifier
  {
    $1->AddNode(
      new VariableDeclaration(
        static_cast<VariableDeclaration*>($1->GetFirst())->GetType(),
        $3, nullptr));
  }
  | VariableDeclarationListPart ',' Identifier '=' Expression
  {
    $1->AddNode(
      new VariableDeclaration(
        static_cast<VariableDeclaration*>($1->GetFirst())->GetType(),
        $3, $5));
  }
  ;

/*IdentifierList
  : Identifier { $$ = new StringList(); $$->AddString($1); }
  | IdentifierList ',' Identifier { $1->AddString($3); }
  ;*/

Statement
  : Expression ';' { $$ = new ExpressionStatement($1); }
  ;

Expression
  : Expression '+' Expression { $$ = new BinaryExpression($1, BinaryExpression::Add, $3); }
  | Identifier '=' Expression { $$ = new AssignmentExpression($1, $3); }
  | TK_PEEK '(' Expression ')' { $$ = new PeekExpression($3); }
  | TK_POP '(' ')' { $$ = new PopExpression(); }
  | TK_PUSH '(' Expression ')' { $$ = new PushExpression($3); }
  | Identifier { $$ = new IdentifierExpression($1); }
  | IntegerLiteral { $$ = new IntegerLiteralExpression($1); }
  ;

%%

