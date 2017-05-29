/* 
// regex parser
// 
// regex EBNF

regex ::= or-expr $ ;

or-expr ::= term ( '|' term )* ;

term ::= primary* ;

primary ::= character
         |  character '*'
         |  character '+'
         |  character '?'
         ;

character ::= '['  char-class ']'
           |  '[^' char-class ']'
           |  '[:' posix-class ':]'
           |  '(' or-expr ')'
           |  literal
           |  meta-char
           ;

char-class ::= (char-set | char-range )* ;

char-set ::= letter | meta-char | operator | '\' operator2 ;

char-range ::= [a-z] '-' [a-z]
            |  [A-Z] '-' [A-Z]
            |  [0-9] '-' [0-9]
            ;

meta-char ::= '.' | '^' | '$' ;

posix-class ::= ? ;

operator ::= '|' | '*' | '+' | '?' | '(' | ')' | '[' ;
operator2 ::=  ']' | '\' ;

literal ::= letter
         |  '\' ( meta-char | operator | operator2 )
         ;

letter  ::= <any character without meta-char & operator> ;

*/



#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<stdarg.h>
#include<ctype.h>
#include<stddef.h>


#include "lex_parse.h"

#ifdef CC_OLD
static RegexAST* DotAST = &(struct RegexAST){Dot,NULL,NULL,'\0','\0','\0'};
#else
static RegexAST* DotAST = &(struct RegexAST){Dot,{{NULL,NULL}}};
#endif


static char_type** source; // 入力文字列を保持する




// AST補助関数

static inline RegexAST* makeAST(enum ASTType type,RegexAST* lhs,RegexAST* rhs){
	RegexAST* ret = (RegexAST*)malloc(sizeof(RegexAST));
	ret->type = type;
	ret->lhs = lhs; ret->rhs = rhs;
	return ret;
}

static inline RegexAST* makeChar(char_type c){
	RegexAST* ret = makeAST(Char,NULL,NULL);
	ret->c = c;
	return ret;
}

static inline RegexAST* makeRange(char_type b,char_type e){
	RegexAST* ret = makeAST(Range,NULL,NULL);
	ret->begin = b; ret->end = e;
	return ret;
}

void freeAST(RegexAST* ast){
	if(ast == NULL || ast->type == Dot) return;
	switch(ast->type){
		case Connect: freeAST(ast->lhs);
					  freeAST(ast->rhs); break;
		case Star:    freeAST(ast->lhs); break;	
		case Plus:    freeAST(ast->lhs); break;
		case Question:freeAST(ast->lhs); break;
		case Or:      freeAST(ast->lhs);
					  freeAST(ast->rhs); break;
		case Not:     freeAST(ast->lhs); break;
		default: break;
	}

	free(ast);
}

void printAST(RegexAST* ast,int indent){
	if(ast == NULL) return;
	switch(ast->type){
		case Connect: printf("%*sConnect{\n",indent,"");
					  printAST(ast->lhs,indent+1);
					  printf("%*s<&>\n",indent+1,""); 
					  printAST(ast->rhs,indent+1);
					  printf("%*s}\n",indent,"");       break;
		case Star:    printf("%*sStar{\n",indent,""); 
					  printAST(ast->lhs,indent+1);
					  printf("%*s}\n",indent,"");       break;	
		case Plus:    printf("%*sPlus{\n",indent,""); 
					  printAST(ast->lhs,indent+1);
					  printf("%*s}\n",indent,"");       break;
		case Question:printf("%*sQuestion{\n",indent,""); 
					  printAST(ast->lhs,indent+1);
					  printf("%*s}\n",indent,"");       break;
		case Or:      printf("%*sOr{\n",indent,""); 
					  printAST(ast->lhs,indent+1);
					  printf("%*s<|>\n",indent+1,""); 
					  printAST(ast->rhs,indent+1);
					  printf("%*s}\n",indent,"");       break;
		case Not:     printf("%*sNot{\n",indent,""); 
					  printAST(ast->lhs,indent+1); 
					  printf("%*s}\n",indent,"");       break;
		case Char:    printf("%*sChar : %c\n",indent,"",ast->c); break;
		case Dot:     printf("%*sDot\n",indent,"");     break;
		case Range:   printf("%*sRange: %c - %c\n",indent,"",ast->begin,ast->end); break;
		default:
			printf("ERROR\n");
	}
}






//字句解析関数

void setLex(char_type** str){
	source = str;
}

static inline char_type consumeToken(void){ // トークンを消費して、消費したトークンを返す
	char_type ret = **source;
	(*source)++;
	return ret;
}

static inline char_type lookToken(int k){ // トークンを先読みする、1は消費予定のトークン
	return (*source)[k-1];
}

static inline bool isMeta(char_type c){
	return c == '.' || c == '^' || c == '$' ;
}

static inline bool isOp(char_type c){
	return c == '|' || c == '*' || c == '+' || c == '?'
		|| c == '(' || c == ')' || c == '[' ;
}

static inline bool isOp2(char_type c){
	return c == ']' || c == '\\' ;
}

/*
static inline bool isRange(void){
	if(lookToken(2) == '-'){
		if(islower(lookToken(1))){
			if(islower(lookToken(3))) return true;
		}
		else if(isupper(lookToken(1))){
			if(isupper(lookToken(3))) return true;
		}
		else if(isdigit(lookToken(1))){
			if(isdigit(lookToken(3))) return true;
		}
	}

	return false;
}
*/

static inline bool isRange(void){
	return lookToken(2) == '-' && lookToken(3) != ']';
}



// 構文解析関数

static RegexAST* parseOrExpr(void); // or式のパース

static RegexAST* parseCharClass(void){
	if(lookToken(1) == ']'){
		// error
		printf("error parseCharClass. empty."); exit(1);
	}
	
	RegexAST* ch;
	if(isRange()){
		char_type b = consumeToken();
		consumeToken(); // take '-'
		char_type e = consumeToken();
		ch = makeRange(b,e);
	}
	else if(lookToken(1) == '\\'){
		if(isOp2(lookToken(2))){
			consumeToken(); // take '\'
			ch = makeChar(consumeToken());
		}
		else {
			//error
			printf("error parseCharClass. first."); exit(1);
		}
	}
	else {
		ch = makeChar(consumeToken());
	}

	while(lookToken(1) != ']' && lookToken(1) != '\0'){
		if(isRange()){
			char_type b = consumeToken();
			consumeToken(); // take '-'
			char_type e = consumeToken();
			ch = makeAST(Or,ch,makeRange(b,e));
		}
		else if(lookToken(1) == '\\'){
			if(isOp2(lookToken(2))){
				consumeToken(); // take '\'
				ch = makeAST(Or,ch,makeChar(consumeToken()));
			}
			else {
				//error
				printf("error parseCharClass. in loop."); exit(1);
			}
		}
		else {
			ch = makeAST(Or,ch,makeChar(consumeToken()));
		}
	}

	return ch;

}

static RegexAST* parseLitAndMeta(void){
	RegexAST* ch;
	if(lookToken(1) == '\\'){
		consumeToken(); // take '\'
		char_type c = consumeToken(); // take meta-char
		if(isMeta(c) || isOp(c) || isOp2(c)) ch = makeChar(c);
		else ch = NULL; // error or other mean
	}
	else {
		char_type c = consumeToken();
		if(isMeta(c)){
			if(c == '.') ch = DotAST;
			else ch = NULL; // error , todo:^ $
		}
		else {
			ch = makeChar(c);
		}
	}

	return ch;
}

static RegexAST* parseCharacter(void){
	RegexAST* ch;
	char_type h = lookToken(1);
	if(h == '['){
		consumeToken(); // take '['
		char_type h2 = lookToken(1);
		switch(h2){
			case '^': consumeToken(); ch = makeAST(Not,parseCharClass(),NULL); break;
			//case ':': nextToken(); break; // POSIX class
			default: ch = parseCharClass();
		}
		if(lookToken(1) != ']'){
			// error
			printf("error parseCharacter. ']' \n"); exit(1);
		}
		consumeToken(); // take ']'
	}
	else if(h == '('){
		consumeToken(); // take '('
		ch = parseOrExpr();
		if(lookToken(1) != ')'){
			// error
			printf("error parseCharacter. ')' \n"); exit(1);
		}
		consumeToken(); // take ')'
	}
	else { // literal or meta
		ch = parseLitAndMeta();
	}

	return ch;
}


static RegexAST* parsePrimary(void){
	RegexAST* ch = parseCharacter();
	switch(lookToken(1)){
		case '*': consumeToken(); ch = makeAST(Star,ch,NULL); break;
		case '+': consumeToken(); ch = makeAST(Plus,ch,NULL); break;
		case '?': consumeToken(); ch = makeAST(Question,ch,NULL); break;
	}

	return ch;
}

static RegexAST* parseTerm(void){
	RegexAST* ret = NULL;
	char_type l = lookToken(1);
	while( l != '|' && l != '\0' && l != ')'){
		ret = ret ? makeAST(Connect,ret,parsePrimary()) : parsePrimary();
		l = lookToken(1);
	}

	return ret;
}

static RegexAST* parseOrExpr(void){
	RegexAST* term = parseTerm();

	while(lookToken(1) == '|'){
		consumeToken(); // take '|'
		term = makeAST(Or,term,parseTerm());
	}

	return term;
}

RegexAST* parseRegex(char_type** str){
	setLex(str);

	RegexAST* ret = parseOrExpr();
	
	if(lookToken(1) != '\0'){
		// error
		printf("error parseRegex.\n"); exit(1);
	}

	return ret;
}

