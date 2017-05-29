#ifndef REGEX_VM_PARSE
#define REGEX_VM_PARSE

#include "lex_vm.h"

enum ASTType{
	Connect,
	Star,
	Plus,
	Question,
	Or,
	Not,
	Char,
	Dot,
	Range
};

#ifdef CC_OLD
typedef struct RegexAST{
	enum ASTType type;
	struct RegexAST* lhs;
	struct RegexAST* rhs;
	char_type c;
	char_type begin,end;
} RegexAST;
#else
typedef struct RegexAST{
	enum ASTType type;
	union {
		struct{
			struct RegexAST* lhs;
			struct RegexAST* rhs;
		};
		char_type c;
		struct{
			char_type begin,end;
		};
	};
} RegexAST;
#endif


void printAST(RegexAST* ast,int indent);

void setLex(char_type** str);

RegexAST* parseRegex(char_type** str);

void freeAST(RegexAST* ast);


#endif // REGEX_VM_PARSE
