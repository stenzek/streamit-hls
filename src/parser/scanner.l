%option noc++
%option yylineno

%{
#include "parser/scanner_helpers.h"
#include "parser/ast.h"
#include "parser/parser.h"

// Clear out the line number when resetting.
// This is due to the global state, when we move to a reentrant scanner this can be dropped.
#define YY_USER_INIT do { yylineno = 1; yylloc.first_column = 0; yylloc.last_column = 0; yylloc.first_line = 1; yylloc.last_line = 1; } while(0);

// Update the line number in the parser location for each token.
#define YY_USER_ACTION do { yylloc.first_column = 0; yylloc.last_column = 0; yylloc.first_line = yylineno; yylloc.last_line = yylineno; } while (0);

%}

%%

 /* Word tokens */
filter { return TK_FILTER; }
pipeline { return TK_PIPELINE; }
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

 /* Boolean literal */
true { yylval.boolean_literal = true; return TK_BOOLEAN_LITERAL; }
false { yylval.boolean_literal = false; return TK_BOOLEAN_LITERAL; }

 /* Integer literal */
[\+\-]?0x[0-9A-Fa-f]+ { yylval.integer_literal = ScannerHelpers::ParseIntegerLiteral(yytext, 16); return TK_INTEGER_LITERAL; }
[\+\-]?[0-9]+ { yylval.integer_literal = ScannerHelpers::ParseIntegerLiteral(yytext, 10); return TK_INTEGER_LITERAL; }

 /* Identifier */
[a-zA-Z][a-zA-Z0-9]* { yylval.identifier = strdup(yytext); return TK_IDENTIFIER; }

 /* Single character tokens */
"{" { return '{'; }
"}" { return '}'; }
"(" { return '('; }
")" { return ')'; }
";" { return ';'; }
"<" { return '<'; }
">" { return '>'; }
"=" { return '='; }
"!" { return '!'; }
"," { return ','; }
"?" { return '?'; }
":" { return ':'; }

 /* Whitespace/comments */
[ \r\n\t]       /* skip whitespace */

 /* Single-line comment */
\/\/[^\n]+\n

 /* Multi-line comment */
\/\*(.*)\*\/

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
