%option noc++
%option yylineno

%{
#include "parser/scanner_helpers.h"
#include "parser/ast.h"
#include "parser/parser_defines.h"
#include "parser/parser_tokens.h"

// Clear out the line number when resetting.
// This is due to the global state, when we move to a reentrant scanner this can be dropped.
#define YY_USER_INIT do { yylineno = 1; yylloc.first_column = 0; yylloc.last_column = 0; yylloc.first_line = 1; yylloc.last_line = 1; } while(0);

// Update the line number in the parser location for each token.
#define YY_USER_ACTION do { yylloc.first_column = 0; yylloc.last_column = 0; yylloc.first_line = yylineno; yylloc.last_line = yylineno; } while (0);

static void comment();

%}

%%

 /* Comments */
"/*" { comment(); }
"//".* { }

 /* Word tokens */
filter { return TK_FILTER; }
pipeline { return TK_PIPELINE; }
splitjoin { return TK_SPLITJOIN; }
stateful { return TK_STATEFUL; }
peek { return TK_PEEK; }
pop { return TK_POP; }
push { return TK_PUSH; }
add { return TK_ADD; }
split { return TK_SPLIT; }
join { return TK_JOIN; }
init { return TK_INIT; }
prework { return TK_PREWORK; }
work { return TK_WORK; }
boolean { return TK_BOOLEAN; }
bit { return TK_BIT; }
int { return TK_INT; }
float { return TK_FLOAT; }
complex { return TK_COMPLEX; }
if { return TK_IF; }
else { return TK_ELSE; }
for { return TK_FOR; }
do { return TK_DO; }
while { return TK_WHILE; }
continue { return TK_CONTINUE; }
break { return TK_BREAK; }
return { return TK_RETURN; }
void { return TK_VOID; }
duplicate { return TK_DUPLICATE; }
roundrobin { return TK_ROUNDROBIN; }

 /* Digraphs */
"&&" { return TK_LOGICAL_AND; }
"||" { return TK_LOGICAL_OR; }
"==" { return TK_EQUALS; }
"!=" { return TK_NOT_EQUALS; }
"++" { return TK_INCREMENT; }
"--" { return TK_DECREMENT; }
"<=" { return TK_LTE; }
">=" { return TK_GTE; }
"->" { return TK_ARROW; }
"<<" { return TK_LSHIFT; }
">>" { return TK_RSHIFT; }
"+=" { return TK_ADD_ASSIGN; }
"-=" { return TK_SUB_ASSIGN; }
"*=" { return TK_MUL_ASSIGN; }
"/=" { return TK_DIV_ASSIGN; }
"%=" { return TK_MOD_ASSIGN; }
"^=" { return TK_BITWISE_XOR_ASSIGN; }
"|=" { return TK_BITWISE_OR_ASSIGN; }
"&=" { return TK_BITWISE_AND_ASSIGN; }
"<<=" { return TK_LSHIFT_ASSIGN; }
">>=" { return TK_RSHIFT_ASSIGN; }

 /* Arbitrary precision data types */
apint[1-9][0-9]* { yylval.apint_num_bits = ScannerHelpers::ParseIntegerLiteral(yytext + 5, 10); return TK_APINT; }

 /* Boolean literal */
true { yylval.boolean_literal = true; return TK_BOOLEAN_LITERAL; }
false { yylval.boolean_literal = false; return TK_BOOLEAN_LITERAL; }

 /* Integer literal */
0x[0-9A-Fa-f]+ { yylval.integer_literal = ScannerHelpers::ParseIntegerLiteral(yytext, 16); return TK_INTEGER_LITERAL; }
[0-9]+ { yylval.integer_literal = ScannerHelpers::ParseIntegerLiteral(yytext, 10); return TK_INTEGER_LITERAL; }

 /* Identifier */
[a-zA-Z][a-zA-Z0-9_]* { yylval.identifier = strdup(yytext); return TK_IDENTIFIER; }

 /* Single character tokens */
"{" { return '{'; }
"}" { return '}'; }
"(" { return '('; }
")" { return ')'; }
"," { return ','; }
";" { return ';'; }
"<" { return '<'; }
">" { return '>'; }
"=" { return '='; }
"!" { return '!'; }
"?" { return '?'; }
":" { return ':'; }
"+" { return '+'; }
"-" { return '-'; }
"*" { return '*'; }
"/" { return '/'; }
"%" { return '%'; }
"&" { return '&'; }
"|" { return '|'; }
"^" { return '^'; }
"[" { return '['; }
"]" { return ']'; }

 /* Skip Whitespace */
[ \t\v\n\f] { }

<<EOF>>						{ BEGIN(0); return 0; }

 /* Throw error on unrecognized token */
.                           {
                ScannerHelpers::ReportScannerError(yylineno, "unrecognized token: %s", yytext);
                return 0;
              }
%%

int yywrap()
{
  // We don't have any more inputs after end-of-file.
  return 1;
}

static void comment()
{
  int c;
  while ((c = yyinput()) != 0)
  {
    if (c == '*')
    {
      while ((c = yyinput()) == '*')
        ;
      if (c == '/')
        return;
      if (c == 0)
        break;
    }
  }
  ScannerHelpers::ReportScannerError(yylineno, "unterminated comment");
}