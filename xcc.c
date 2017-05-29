#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <regex.h>

#include "lex_vm.h"

struct AST {
    char        *ast_type;   // 生成規則を区別
    struct AST	*parent;     // 親へのバックポインタ
    int	       	nth;         // 自分が何番目の兄弟か
    int         num_child;   // 子ノードの数
    struct AST  **child;     // 子ノードポインタの配列
    char        *lexeme;     // 葉ノード用 (整数，文字，文字列の定数，識別子）
};

/* ------------------------------------------------------- */
static void print_nspace (int n);
static struct AST* create_AST (char *ast_type, int num_child, ...);
static struct AST* create_leaf (char *ast_type, char *lexeme);
static struct AST* add_AST (struct AST *ast, int num_child, ...);
static void show_AST (struct AST *ast, int depth);
static void unparse_AST (struct AST *ast, int depth);

static void unparse_error (struct AST *ast);

static struct AST* parse_translation_unit (void);
static struct AST* parse_type_specifier (void);
static struct AST* parse_declarator (void);
static struct AST* parse_direct_declarator (void);
static struct AST* parse_parameter_declaration (void);
static struct AST* parse_statement (void);
static struct AST* parse_compound_statement (void); 
static struct AST* parse_exp (void);
static struct AST* parse_exp1 (void);
static struct AST* parse_exp2 (void);
static struct AST* parse_exp3 (void);
static struct AST* parse_exp4 (void);
static struct AST* parse_exp5 (void);
static struct AST* parse_exp6 (void);
static struct AST* parse_exp7 (void);
static struct AST* parse_exp8 (void);
static struct AST* parse_exp9 (void);
static struct AST* parse_primary (void);
static struct AST* parse_argument_expression_list (void);

static char* map_file (char *filename);
static void* copy_string_region_int (char *s, int start, int end);
static int set_token_int (char *ptr, int begin, int end, int kind, int off);
static void create_tokens (char *ptr);
static void dump_tokens ();
/* ------------------------------------------------------- */
// データ構造と変数

#define MAX_TOKENS 10000
struct token {
    int kind;
    int offset_begin; 
    int offset_end;
    char *lexeme;
};
enum token_kind {
	TK_ERROR     = 65533, // 受理できない文字に対するエラー
	TK_COM_BEGIN = 65534, // コメントの先頭
	TK_COM_END   = 65535, // コメントの末尾
    TK_UNUSED    = 0,
    TK_ID        = 1,
    TK_INT       = 2,
    TK_CHAR      = 3,
    TK_STRING    = 4,
    TK_KW_CHAR   = 5,  // char
    TK_KW_ELSE   = 6,  // else
    TK_KW_GOTO   = 7,  // goto
    TK_KW_IF     = 8,  // if
    TK_KW_INT    = 9,  // int
    TK_KW_RETURN = 10, // return
    TK_KW_VOID   = 11, // void
    TK_KW_WHILE  = 12, // while
    TK_OP_EQ     = 13, // ==
    TK_OP_AND    = 14, // &&
    TK_OP_OR     = 15, // ||
    TK_COMMENT   = 16,  // デバッグ用
    TK_WHITESPACE = 17, // デバッグ用
    // 以下は名前を付けずにそのまま使う
    // ';' ':' '{' '}' ',' '=' '(' ')' '&' '!' '-' '+' '*' '/' '<'
};

SymbolElement token_table[] = { // 文字長が同じ時は上に書いたトークンが採用される,探索は最長マッチ
	{ "/\\*"                   , TK_COM_BEGIN },
	{ "\\*/"                   , TK_COM_END   },
	{ "char"                   , TK_KW_CHAR   },
	{ "else"                   , TK_KW_ELSE   },
	{ "goto"                   , TK_KW_GOTO   },
	{ "if"                     , TK_KW_IF     },
	{ "int"                    , TK_KW_INT    },
	{ "return"                 , TK_KW_RETURN },
	{ "void"                   , TK_KW_VOID   },
	{ "while"                  , TK_KW_WHILE  },
	{ "=="                     , TK_OP_EQ     }, 
	{ "&&"                     , TK_OP_AND    }, 
	{ "\\|\\|"                 , TK_OP_OR     },
	{ ";"   , ';' }, { ":"   , ':' }, { "{"   , '{' },
	{ "}"   , '}' }, { ","   , ',' }, { "="   , '=' },
	{ "\\(" , '(' }, { "\\)" , ')' }, { "&"   , '&' },
	{ "!"   , '!' }, { "-"   , '-' }, { "\\+" , '+' },
	{ "\\*" , '*' }, { "/"   , '/' }, { "<"   , '<' },
	{ "0|[1-9][0-9]*"                          , TK_INT    },
	{ "'(\\\\n|\\\\'|\\\\\\\\|[^\\\\'])'"      , TK_CHAR   },
	{ "\"(\\\\n|\\\\\"|\\\\\\\\|[^\\\\\"])*\"" , TK_STRING },
	{ "[_a-zA-Z][_a-zA-Z0-9]*"                 , TK_ID     },
	{ "[ \n\r\t]"                              , TK_UNUSED },
	{ "."                                      , TK_ERROR  },
};

char *token_kind_string[] = {
    "UNUSED", "ID", "INT", "CHAR", "STRING",
    "char", "else", "goto", "if", "int",
    "return", "void", "while", "==", "&&", "||",
    [';'] = ";", [':'] = ":", ['{'] = "{", ['}'] = "}", [','] = ",",
    ['='] = "=", ['('] = "(", [')'] = ")", ['&'] = "&", ['!'] = "!",
    ['-'] = "-", ['+'] = "+", ['*'] = "*", ['/'] = "/", ['<'] = "<",
};

char* token_kind_name[] = {
	"TK_UNUSED",   "TK_ID",        "TK_INT",       "TK_CHAR",
	"TK_STRING",   "TK_KW_CHAR",   "TK_KW_ELSE",   "TK_KW_GOTO", 
	"TK_KW_IF",    "TK_KW_INT",    "TK_KW_RETURN", "TK_KW_VOID", 
	"TK_KW_WHILE", "TK_KW_OP_EQ",  "TK_OP_AND",    "TK_OP_OR",
    [';'] = ";", [':'] = ":", ['{'] = "{", ['}'] = "}", [','] = ",",
    ['='] = "=", ['('] = "(", [')'] = ")", ['&'] = "&", ['!'] = "!",
    ['-'] = "-", ['+'] = "+", ['*'] = "*", ['/'] = "/", ['<'] = "<",
};


static struct token tokens [MAX_TOKENS];
static int tokens_index = 0;
static struct token *token_p; // for parsing

/* ------------------------------------------------------- */

static void
print_nspace (int n)
{
    printf ("%*s", n, "");
}

static struct AST*
create_AST (char *ast_type, int num_child, ...)
{
    va_list ap;
    struct AST *ast;
    ast = malloc (sizeof (struct AST));
    ast->parent = NULL;
    ast->nth    = -1;
    ast->ast_type = ast_type;
    ast->num_child = num_child;
    ast->lexeme = NULL;
    va_start (ap, num_child);
    if (num_child == 0) {
        ast->child = NULL;
    } else {
        int i;
    	ast->child = malloc (sizeof(struct AST *) * num_child);
	for (i = 0; i < num_child; i++) {
	    struct AST *child = va_arg (ap, struct AST *);
	    ast->child [i] = child;
	    if (child != NULL) {
		child->parent = ast;
		child->nth    = i;
	    }
    	}
    }
    va_end (ap);
    return ast;
}

static struct AST*
create_leaf (char *ast_type, char *lexeme)
{
    struct AST *ast;
    ast = malloc (sizeof (struct AST));
    ast->parent    = NULL;
    ast->nth       = -1;
    ast->ast_type  = ast_type;
    ast->num_child = 0;
    ast->child     = NULL;
    ast->lexeme    = lexeme;
    return ast;
}

static struct AST*
add_AST (struct AST *ast, int num_child, ...)
{
    va_list ap;
    int i, start = ast->num_child;
    ast->num_child += num_child;
    assert (num_child > 0);
    ast->child = realloc (ast->child, sizeof(struct AST *) * ast->num_child);
    va_start (ap, num_child);
    for (i = start; i < ast->num_child; i++) {
        struct AST *child = va_arg (ap, struct AST *);
        ast->child [i] = child;
        if (child != NULL) {
            child->parent = ast;
            child->nth    = i;
        }
    }
    va_end (ap);
    return ast;
}

static void
show_AST (struct AST *ast, int depth)
{
    int i;
    print_nspace (depth);
    if (   !strcmp (ast->ast_type, "TK_ID")
        || !strcmp (ast->ast_type, "TK_INT")
        || !strcmp (ast->ast_type, "TK_CHAR")
        || !strcmp (ast->ast_type, "TK_STRING")) {
        printf ("%s (%s)\n", ast->ast_type, ast->lexeme); 
    } else {
        printf ("%s\n", ast->ast_type); 
    }
        
    for (i = 0; i < ast->num_child; i++) {
        if (ast->child [i] != NULL) {
            show_AST (ast->child [i], depth +1);
        }
    }
}
/* ------------------------------------------------------- */
// 構文解析

static void
parse_error (void)
{
    fprintf (stderr, "parse error (%d-%d): %s (%s)\n",
             token_p->offset_begin, token_p->offset_end,
             token_kind_string [token_p->kind], token_p->lexeme);
    exit (1);
}

static int
lookahead (int i)
{
	// printf("look: %s\n",tokens[tokens_index+i-1].lexeme);
    return tokens [tokens_index + i - 1].kind;
}

static struct token*
next_token (void)
{
    token_p = &tokens [++tokens_index];
    assert (tokens_index < MAX_TOKENS);
    return token_p;
}

static struct token*
reset_tokens (void)
{
    tokens_index = 0;
    token_p = &tokens [tokens_index];
    return token_p;
}

static void
consume_token (enum token_kind kind)
{
    if (lookahead (1) == kind) {
        next_token ();
    } else {
		fprintf(stderr,"expect token : %s\n",token_kind_string[kind]);
        parse_error ();
    }
}



static struct token* eat_token(void){
	assert(tokens_index + 1 < MAX_TOKENS);
	return token_p = &tokens[tokens_index++];
}

static void consume_and_add(struct AST** ast,int kind){
	consume_token(kind);
	*ast = add_AST(*ast,1,create_leaf(token_kind_name[kind],token_kind_string[kind]));
}

// exp,exp2-9のfirst set
static int expect_exp(enum token_kind k){
	return k == '&' || k == '*' || k == '+' || k == '-' || k == '!' 
		|| k == '('
		|| k == TK_INT || k == TK_CHAR || k == TK_STRING || k == TK_ID
		;
}

static int expect_primary(enum token_kind k){
	return k == '('
		|| k == TK_INT || k == TK_CHAR || k == TK_STRING || k == TK_ID;
}

static int expect_exp1(enum token_kind k){
	return expect_primary(k);
}


// type_specifier : "void" | "char" | "int" ;
static struct AST* parse_type_specifier (void){
	struct AST* ast;
	struct token* t;
	switch(lookahead(1)){
		case TK_KW_INT: case TK_KW_CHAR: case TK_KW_VOID:
			t = eat_token();
			ast = create_AST("type_specifier",1, create_leaf(token_kind_name[t->kind],t->lexeme));
			break;
		default: parse_error();
	}

	return ast;
}


// declarator : ( "*" )* direct_declarator ;
static struct AST* parse_declarator (void){
	struct AST* ast = NULL;
	struct AST* child = NULL;
	if(lookahead(1) == '*'){
		ast = create_AST("declarator",0);
		while(lookahead(1) == '*'){
			consume_and_add(&ast,'*');
		}
	}
	switch(lookahead(1)){
		case TK_ID:
		case '(':
			child = parse_direct_declarator();
			break;
		default: parse_error();
	}

	if(ast == NULL) return child;

	ast = add_AST(ast,1,child);
	return ast;
}


// direct_declarator : ( IDENTIFIER | "(" declarator ")" ) ( "(" [ parameter_declaration ( "," parameter_declaration )* ] ")" )* ;
static struct AST* parse_direct_declarator (void){
	struct AST* ast = create_AST("direct_declarator",0);
	struct AST* child,*param;
	struct token* t;
	switch(lookahead(1)){
		case TK_ID:
			t = eat_token();
			child = create_leaf("TK_ID",t->lexeme);
			ast = add_AST(ast,1,child);
			break;
		case '(':
			consume_token('(');
			ast = add_AST(ast,1,parse_declarator());
			consume_token(')');
			break;
		default: parse_error();
	}
	

	while(lookahead(1) == '('){
		consume_token('('); 
		param = create_AST("direct_declarator_parameter",0);
		
		if(lookahead(1) == TK_KW_INT || lookahead(1) == TK_KW_CHAR || lookahead(1) == TK_KW_VOID){
			child = parse_parameter_declaration();
			param = add_AST(param,1,child);

			while(lookahead(1) == ','){
				consume_token(',');
				child = parse_parameter_declaration();
				param = add_AST(param,1,child);
			}

		}

		ast = add_AST(ast,1,param);

		consume_token(')');
	}

	return ast;
}


// parameter_declaration : type_specifier declarator ;
static struct AST* parse_parameter_declaration (void){
	struct AST *child0,*child1;
	if(lookahead(1) == TK_KW_INT || lookahead(1) == TK_KW_CHAR || lookahead(1) == TK_KW_VOID){
		child0 = parse_type_specifier();
		child1 = parse_declarator();
	}
	else parse_error();

	struct AST* ast = create_AST("parameter_declaration",2,child0,child1);

	return ast;
}


/*
statement : IDENTIFIER ":" // 注1
          | compound_statement
          | "if" "(" exp ")" statement [ "else" statement ] // 注2
          | "while" "(" exp ")" statement
          | "goto" IDENTIFIER ";"
          | "return" [ exp ] ";"
          | [ exp ] ";" 
          ;
*/
static struct AST* parse_statement (void){
	struct AST* ast = NULL;
	struct AST* child = NULL;
	struct token* t;

	if(lookahead(1) == TK_ID && lookahead(2) == ':'){ // label
		t = eat_token();
		child = create_leaf("TK_ID",t->lexeme);
		ast = create_AST("statement_label",1,child);
		consume_token(':');
		return ast;
	}

	if(expect_exp(lookahead(1))){ // exp
		child = parse_exp();
		ast = create_AST("statement_exp",1,child);
		consume_token(';');
		return ast;
	}

	switch(lookahead(1)){ // other
		case ';': // empty exp
			ast = create_AST("statement_exp",1,create_leaf("exp_empty"," "));
			consume_token(';');
			break;
		case '{':
			ast = parse_compound_statement();
			break;
		case TK_KW_IF:
			ast = create_AST("statement_if",0);
			consume_token(TK_KW_IF);
			consume_token('(');
			ast = add_AST(ast,1,parse_exp());
			consume_token(')');
			ast = add_AST(ast,1,parse_statement());
			
			if(lookahead(1) == TK_KW_ELSE){
				consume_token(TK_KW_ELSE);
				ast = add_AST(ast,1,parse_statement());
			}

			break;
		case TK_KW_WHILE:
			ast = create_AST("statement_while",0);
			consume_token(TK_KW_WHILE); 
			consume_token('(');
			ast = add_AST(ast,1,parse_exp());
			consume_token(')');
			ast = add_AST(ast,1,parse_statement());
			break;
		case TK_KW_GOTO:
			ast = create_AST("statement_goto",0);
			consume_token(TK_KW_GOTO);
			t = eat_token();
			ast = add_AST(ast,1,create_leaf("TK_ID",t->lexeme));
			consume_token(';');
			break;
		case TK_KW_RETURN:
			ast = create_AST("statement_return",0);
			consume_token(TK_KW_RETURN);
			if(expect_exp(lookahead(1))){
				ast = add_AST(ast,1,parse_exp());
			}
			consume_token(';');
			break;
		default: parse_error();
	}

	return ast;
}


// compound_statement : "{" (type_specifier declarator ";")* ( statement )* "}" ;
static struct AST* parse_compound_statement (void){
	struct AST* ast = create_AST("compound_statement",0);

	consume_token('{'); 

	while(lookahead(1) == TK_KW_INT || lookahead(1) == TK_KW_CHAR || lookahead(1) == TK_KW_VOID){ // decl
		ast = add_AST(ast,1,parse_type_specifier());
		ast = add_AST(ast,1,parse_declarator());
		consume_token(';');
	}

	while(lookahead(1) == '{' || lookahead(1) == ';'
		|| lookahead(1) == TK_KW_IF || lookahead(1) == TK_KW_WHILE
		|| lookahead(1) == TK_KW_GOTO || lookahead(1) == TK_KW_RETURN
		|| expect_exp(lookahead(1))
	){ // stmt
		ast = add_AST(ast,1,parse_statement());
	}

	consume_token('}');

	return ast;
}

// exp : exp9 ;
static struct AST* parse_exp (void){
	struct AST* ast = create_AST("exp",0);
	
	if(expect_exp(lookahead(1))){
		ast = add_AST(ast,1,parse_exp9());
	}
	else parse_error();

	return ast;
}


// exp9 : exp8 ( "=" exp8 )* ;
static struct AST* parse_exp9 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp8();
	}
	else parse_error();

	if(lookahead(1) == '='){
		ast = create_AST("exp9",1,tmp);

		while(lookahead(1) == '='){
			consume_token('=');
			ast = add_AST(ast,1,parse_exp8());
		}

		return ast;
	}
	
	return tmp;
}

// exp8 : exp7 ( "||" exp7 )* ;
static struct AST* parse_exp8 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp7();
	}
	else parse_error();

	if(lookahead(1) == TK_OP_OR){
		ast = create_AST("exp8",1,tmp);

		while(lookahead(1) == TK_OP_OR){
			consume_token(TK_OP_OR);
			ast = add_AST(ast,1,parse_exp7());
		}

		return ast;
	}
	
	return tmp;
}

// exp7 : exp6 ( "&&" exp6 )* ;
static struct AST* parse_exp7 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp6();
	}
	else parse_error();

	if(lookahead(1) == TK_OP_AND){
		ast = create_AST("exp7",1,tmp);

		while(lookahead(1) == TK_OP_AND){
			consume_token(TK_OP_AND);
			ast = add_AST(ast,1,parse_exp6());
		}

		return ast;
	}
	
	return tmp;
}

// exp6 : exp5 ( "==" exp5 )* ;
static struct AST* parse_exp6 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp5();
	}
	else parse_error();

	if(lookahead(1) == TK_OP_EQ){
		ast = create_AST("exp6",1,tmp);

		while(lookahead(1) == TK_OP_EQ){
			consume_token(TK_OP_EQ);
			ast = add_AST(ast,1,parse_exp5());
		}

		return ast;
	}
	
	return tmp;
}

// exp5 : exp4 ( "<" exp4 )* ;
static struct AST* parse_exp5 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp4();
	}
	else parse_error();

	if(lookahead(1) == '<'){
		ast = create_AST("exp5",1,tmp);

		while(lookahead(1) == '<'){
			consume_token('<');
			ast = add_AST(ast,1,parse_exp4());
		}

		return ast;
	}
	
	return tmp;
}

// exp4 : exp3 ( "+" exp3 | "-" exp3 )* ;
static struct AST* parse_exp4 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp3();
	}
	else parse_error();

	enum token_kind k = lookahead(1);
	if(k == '+' || k == '-'){
		ast = create_AST("exp4",1,tmp);

		for(; k == '+' || k == '-'; k = lookahead(1)){
			consume_and_add(&ast,k);
			ast = add_AST(ast,1,parse_exp3());
		}

		return ast;
	}
	
	return tmp;
}

// exp3 : exp2 ( "*" exp2 | "/" exp2 )* ;
static struct AST* parse_exp3 (void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_exp(lookahead(1))){
		tmp = parse_exp2();
	}
	else parse_error();

	enum token_kind k = lookahead(1);
	if(k == '*' || k == '/'){
		ast = create_AST("exp3",1,tmp);

		for(; k == '*' || k == '/'; k = lookahead(1)){
			consume_and_add(&ast,k);
			ast = add_AST(ast,1,parse_exp2());
		}

		return ast;
	}
	
	return tmp;
}


// exp2 : ( "&" | "*" | "+" | "-" | "!" )* exp1 ;
static struct AST* parse_exp2 (void){
	struct AST* ast=NULL;
	enum token_kind k;

	k = lookahead(1);
	if(k == '&' || k == '*' || k == '+' || k == '-' || k == '!'){
		ast = create_AST("exp2",0);

		for(;k == '&' || k == '*' || k == '+' || k == '-' || k == '!';k = lookahead(1)){
			consume_and_add(&ast,k);
		}
	}

	if(expect_exp1(lookahead(1))){
		if(ast == NULL) ast = parse_exp1();
		else ast = add_AST(ast,1,parse_exp1());
	}

	return ast;
}

// exp1 : primary ( "(" argument_expression_list ")" )* ;
static struct AST* parse_exp1(void){
	struct AST* ast;
	struct AST *tmp;

	if(expect_primary(lookahead(1))){
		tmp = parse_primary();
	}

	if(lookahead(1) == '('){
		ast = create_AST("exp1",1,tmp);

		while(lookahead(1) == '('){
			consume_token('(');
			
			ast = add_AST(ast,1,parse_argument_expression_list());

			consume_token(')');
		}

		return ast;
	}

	return tmp;
}

// primary : INTEGER | CHARACTER | STRING | IDENTIFIER | "(" exp ")" ;
static struct AST* parse_primary(void){
	struct AST* ast;
	struct token* t;

	switch(lookahead(1)){
		case TK_INT:
			t = eat_token();
			ast = create_leaf("TK_INT",t->lexeme);
			break;
		case TK_CHAR:
			t = eat_token();
			ast = create_leaf("TK_CHAR",t->lexeme);
			break;
		case TK_STRING:
			t = eat_token();
			ast = create_leaf("TK_STRING",t->lexeme);
			break;
		case TK_ID:
			t = eat_token();
			ast = create_leaf("TK_ID",t->lexeme);
			break;
		case '(':
			ast = create_AST("primary",0);
			consume_token('(');
			ast = add_AST(ast,1,parse_exp());
			consume_token(')');
			break;
		default: parse_error();
	}

	return ast;
}


// argument_expression_list : [ exp ( "," exp )* ] ;
static struct AST* parse_argument_expression_list(void){
	struct AST* ast = create_AST("argument_expression_list",0);
	if(expect_exp(lookahead(1))){
		ast = add_AST(ast,1,parse_exp());

		while(lookahead(1) == ','){
			consume_token(',');
			ast = add_AST(ast,1,parse_exp());
		}
	}
	else{
		ast = add_AST(ast,1,create_AST("exp",1,create_leaf("exp_empty"," ")));
	}


	return ast;
}

// translation_unit: ( type_specifier declarator ( ";" | compound_statement ))*
static struct AST*
parse_translation_unit (void)
{
    struct AST *ast, *ast1, *ast2, *ast3;

    ast = create_AST ("translation_unit", 0);
    while (1) {
        switch (lookahead (1)) {
        case TK_KW_INT: case TK_KW_CHAR: case TK_KW_VOID:
            ast1 = parse_type_specifier ();
            ast2 = parse_declarator ();
            switch (lookahead (1)) {
            case ';':
                consume_token (';');
				ast = add_AST(ast,1,create_AST("translation_unit_element", 2, ast1, ast2));
                break;
            case '{':
                ast3 = parse_compound_statement ();
				ast = add_AST(ast,1,create_AST("translation_unit_element", 3, ast1, ast2 , ast3));
                break;
            default:
                parse_error ();
                break;
            }
            break;
        default:
            goto loop_exit;
        }
    }
loop_exit:
    return ast;
}


/* ------------------------------------------------------- */

// 字句解析

static char*
map_file (char *filename)
{
    struct stat sbuf;
    char *ptr;

    int fd = open (filename, O_RDWR);
    if (fd == -1) {
        perror ("open");
        exit (1);
    }

    fstat (fd, &sbuf);
#if 0
    printf ("file size = %lld\n", sbuf.st_size);
#endif

    ptr = mmap (NULL, sbuf.st_size + 1, // +1 for '\0'
                PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (ptr == MAP_FAILED) {
        perror ("mmap");
        exit (1);
    }
    ptr [sbuf.st_size] = '\0';
    return ptr;
}

static void*
copy_string_region_int (char *s, int start, int end)
{
    int size = end - start;
    char *buf = malloc (size + 1); // +1 for '\0'
    memcpy (buf, s+start, size);
    buf [size] = '\0';
    return buf;
}

static int
set_token_int (char *ptr, int begin, int end, int kind, int off)
{
    assert (tokens_index < MAX_TOKENS);
    struct token *token_p = &tokens [tokens_index++];
    assert (begin == 0);
    token_p->kind = kind;
    token_p->offset_begin = off + begin;
    token_p->offset_end   = off + end;
    token_p->lexeme = copy_string_region_int (ptr + off, begin, end);
#if 0
    printf ("topen_p->lexeme = |%s|\n", token_p->lexeme);
#endif
    return off + end;
}

static void create_tokens(char* ptr){
	Lexer lex = compileLex(ptr,token_table,sizeof(token_table)/sizeof(SymbolElement));

	Match m;
	int offset = 0;

	while(nextMatch(&lex,&m)){

		//printf("[ tag:%d %.*s]\n",m.tag,m.num,m.str);

		if(m.tag == -1){ // unknown character
			printf("lexical error\n");
			freeLex(&lex);
			exit(1);//goto END;
		}

		else if(m.tag == TK_UNUSED){ // white space
			offset += m.num;
			continue;
		}

		else if(m.tag == TK_COM_BEGIN){ // comment
			offset += m.num;
			while(nextMatch(&lex,&m)){
				offset += m.num;
				if(m.tag == TK_COM_END) break;
			}
			continue;
		}

		//printf("[ tag:%d %.*s]\n",m.tag,m.num,ptr+offset);
		offset = set_token_int(ptr , 0 , m.num , m.tag , offset);
	}

	// success tokenize.
// END:
	freeLex(&lex);
}

static void dump_tokens ()
{
    int i;
    for (i = 0; i < MAX_TOKENS; i++) {
        struct token *token_p = &tokens [i];
        if (token_p->kind == TK_UNUSED)
            break;
        printf ("%5d: %d-%d: %s (%s)\n", i,
                token_p->offset_begin,
                token_p->offset_end,
                token_p->lexeme,
                token_kind_string [token_p->kind]);
    }
}
/* ------------------------------------------------------- */
static void
unparse_error (struct AST *ast)
{
    printf ("something wrong: %s\n", ast->ast_type);
    exit (1);
}

#define CASE(rule) if(!strcmp(ast->ast_type,rule))
#define ECASE(rule) else CASE(rule)
#define OTHERWISE   else

static void indent(int d){
	for(;d--;) printf("    ");
}

static void unparse_AST (struct AST *ast, int depth){
	int i;

	if(ast == NULL){
		printf("!!! null pointer !!!\n");
		return;
	}
	
	int cnum = ast->num_child;
	struct AST** child = ast->child;
	//printf("[%s]\n",ast->ast_type);

	CASE("translation_unit"){
		for(i=0;i<cnum;i++){
			unparse_AST(child[i],depth);
			printf("\n");
		}
	}

	ECASE("translation_unit_element"){
		unparse_AST(child[0],depth);
		printf(" ");
		unparse_AST(child[1],depth);
		if(cnum == 2){
			printf(";\n");
		}
		else {
			unparse_AST(child[2],depth);
			printf("\n");
		}
	}

	ECASE("type_specifier"){
		printf("%s",child[0]->lexeme);
	}

	ECASE("declarator"){
		for(i=0;i<cnum-1;i++) printf("%s",child[i]->lexeme);
		//printf(" ");
		unparse_AST(child[i],depth);
	}

	ECASE("direct_declarator"){
		if(!strcmp(child[0]->ast_type,"TK_ID")){
			printf("%s",child[0]->lexeme);
		}
		else{
			printf("(");
			unparse_AST(child[0],depth);
			printf(")");
		}

		if(cnum > 1){
			for(i=1;i<cnum;i++) unparse_AST(child[i],depth);
		}
	}

	ECASE("direct_declarator_parameter"){
		printf("(");
		if(cnum > 0){
			unparse_AST(child[0],depth);
			for(i=1;i<cnum;i++){
				printf(",");
				unparse_AST(child[i],depth);
			}
		}
		printf(")");
	}

	ECASE("parameter_declaration"){
		unparse_AST(child[0],depth);
		printf(" ");
		unparse_AST(child[1],depth);
	}

	ECASE("statement_label"){
		printf("%s : \n",child[0]->lexeme);
	}

	ECASE("statement_exp"){
		unparse_AST(child[0],depth);
		printf(";\n");
	}

	ECASE("statement_if"){
		printf("if(");
		unparse_AST(child[0],depth);
		printf(")");
		if(!strcmp(child[1]->ast_type,"compound_statement"))
			unparse_AST(child[1],depth);
		else {
			printf("{\n");
			indent(depth+1);
			unparse_AST(child[1],depth+1);
			indent(depth); printf("}\n");
		}

		if(cnum == 3){
			indent(depth);
			printf("else");
			if(!strcmp(child[2]->ast_type,"compound_statement"))
				unparse_AST(child[2],depth);
			else {
				printf("{\n");
				indent(depth+1);
				unparse_AST(child[2],depth+1);
				indent(depth); printf("}\n");
			}
		}

		//printf("\n");
	}

	ECASE("statement_while"){
		printf("while(");
		unparse_AST(child[0],depth);
		printf(") ");
		unparse_AST(child[1],depth);
		//printf("\n");
	}

	ECASE("statement_goto"){
		printf("goto %s;\n",child[0]->lexeme);
	}

	ECASE("statement_return"){
		printf("return");
		if(cnum == 1){
			printf(" ");
			unparse_AST(child[0],depth);
		}
		printf(";\n");
	}

	ECASE("compound_statement"){
		printf("{\n");

		i=0;
		while(i < cnum && !strcmp(child[i]->ast_type,"type_specifier")){
			indent(depth+1);
			unparse_AST(child[i],depth+1);
			printf(" ");
			unparse_AST(child[i+1],depth+1);
			printf(";\n");
			i+=2;
		}
		for(;i<cnum;i++){
			indent(depth+1);
			unparse_AST(child[i],depth+1);
			//printf("\n");
		}

		indent(depth); printf("}\n");
	}

	ECASE("exp"){
		unparse_AST(child[0],depth);
	}

	ECASE("exp9"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf(" = ");
			unparse_AST(child[i],depth);
		}
	}

	ECASE("exp8"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf(" || ");
			unparse_AST(child[i],depth);
		}
	}

	ECASE("exp7"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf(" && ");
			unparse_AST(child[i],depth);
		}
	}

	ECASE("exp6"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf(" == ");
			unparse_AST(child[i],depth);
		}
	}

	ECASE("exp5"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf(" < ");
			unparse_AST(child[i],depth);
		}
	}

	ECASE("exp4"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i+=2){
			printf(" %s ",child[i]->lexeme);
			unparse_AST(child[i+1],depth);
		}
	}

	ECASE("exp3"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i+=2){
			printf(" %s ",child[i]->lexeme);
			unparse_AST(child[i+1],depth);
		}
	}

	ECASE("exp2"){
		for(i=0;i<cnum-1;i++) printf("%s",child[i]->lexeme);
		unparse_AST(child[i],depth);
	}
	
	ECASE("exp1"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf("(");
			unparse_AST(child[i],depth);
			printf(")");
		}
	}
	
	ECASE("primary"){
		printf("(");
		unparse_AST(child[0],depth);
		printf(")");
	}
	
	ECASE("argument_expression_list"){
		unparse_AST(child[0],depth);
		for(i=1;i<cnum;i++){
			printf(",");
			unparse_AST(child[i],depth);
		}
	}

	ECASE("exp_empty"){
		printf(" ");
	}

	ECASE("TK_ID"){
		printf("%s",ast->lexeme);
	}
	
	ECASE("TK_INT"){
		printf("%s",ast->lexeme);
	}
	
	ECASE("TK_CHAR"){
		printf("%s",ast->lexeme);
	}
	
	ECASE("TK_STRING"){
		printf("%s",ast->lexeme);
	}

	OTHERWISE unparse_error(ast);
}



/* ------------------------------------------------------- */
// DOT言語によりASTグラフを出力するための関数群

void print_escape_string(FILE* fp,char* p){
	while( *p != '\0' ){
		if(*p == '"' || *p == '\\') fprintf(fp,"\\");
		fprintf(fp,"%c",*p);

		p++;
	}
}

int output_graph_node(FILE* fp,struct AST* ast,int id){
	assert(ast != NULL);
	
	if(ast->num_child == 0){
		if(ast->lexeme != NULL){
			fprintf(fp,"    node%d [label = \"{%s|",id,ast->ast_type);
			print_escape_string(fp,ast->lexeme);
			fprintf(fp,"}\"]; \n");
		}
		else {
			fprintf(fp,"    node%d [label = \"%s\"]; \n",id,ast->ast_type);
		}
	}
	else {
		if(ast->lexeme != NULL){
			fprintf(fp,"    node%d [label = \"{%s|",id,ast->ast_type);
			print_escape_string(fp,ast->lexeme);
			fprintf(fp,"}\"]; \n");
		}
		else{
			fprintf(fp,"    node%d [label = \"{%s|{", id, ast->ast_type);
		}

		for(int i=0;i < ast->num_child;i++){
			if(i != 0) fprintf(fp,"|");
			fprintf(fp,"<p%d>%i",i,i);
		}
	
		fprintf(fp,"}}\"]; \n");
	}

	int acc = id+1;
	
	for(int i = 0;i < ast->num_child;i++){
		acc = output_graph_node(fp,ast->child[i],acc);
	}

	return acc;
}

int output_graph_edge(FILE* fp,struct AST* ast,int parent_id,int pos,int id){
	if(id != 0){
		fprintf(fp,"    node%d:p%d -> node%d ;\n",parent_id,pos,id);
	}

	int acc = id+1;

	for(int i = 0;i < ast->num_child;i++){
		acc = output_graph_edge(fp,ast->child[i],id,i,acc);
	}

	return acc;
}

void output_graph(char* name,struct AST* ast){

// 変数に入れるとwarning出るのでdefineで定義	
#define DOT_HEADER "digraph AST_graph {              \n"\
		           "    graph [                      \n"\
		           "        charset = \"UTF-8\",     \n"\
		           "        bgcolor = \"#EDEDED\",   \n"\
		           "        rankdir = TB,            \n"\
		           "        nodesep = 1.1,           \n"\
		           "        ranksep = 1.05,          \n"\
		           "    ];                           \n"\
		           "    node [                       \n"\
		           "        shape = record,          \n"\
		           "        // fontname = \"Ricty\", \n"\
		           "        fontsize = 8,            \n"\
		           "    ];                           \n"

#define DOT_FOOTER "}                                \n"

	FILE* fp = fopen(name,"w");
			
	fprintf(fp,DOT_HEADER);

	fprintf(fp,"    // node\n");
	//printf("node\n");
	output_graph_node(fp,ast,0);
	fprintf(fp,"    // edge\n");
	//printf("edge\n");
	output_graph_edge(fp,ast,0,0,0);

	fprintf(fp,DOT_FOOTER);
	
	fclose(fp);
}




/* ------------------------------------------------------- */


int main (int argc, char *argv[])
{
    char *ptr;
    struct AST *ast;

    if (argc < 2) {
        fprintf (stderr, "Usage: %s filename\n", argv[0]);
        exit (1);
    }

    ptr = map_file (argv [1]);
    create_tokens (ptr);
    reset_tokens ();
	//printf("/*++++++++++++++++++++++++++++++++++++\n");
    //dump_tokens ();
	//printf("------------------------------------\n");
    ast = parse_translation_unit ();
    //show_AST (ast, 0);
	//printf("------------------------------------\n");
	//printf("output graph.\n");
	if(argc == 3) output_graph(argv[2],ast);
	//printf("++++++++++++++++++++++++++++++++++++*/\n");
    unparse_AST (ast, 0);
}


