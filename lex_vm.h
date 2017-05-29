#ifndef REGEX_VM
#define REGEX_VM

#include <stdbool.h>
#include <stddef.h>

// ワークスペースを確保するかどうか
#define USE_BUF_FLAG 1

// コンパイラがC11に対応していないとき定義
#define CC_OLD

typedef char char_type; // 文字を表す型

typedef unsigned short vm_addr_type; // アドレスの型
typedef unsigned char  vm_code_type; // バイトコードの型

typedef struct {
	size_t code_size,opcode_size;
	vm_code_type* code;
	vm_addr_type* buf;
} RegexVMCode;

typedef struct {
	char* str;
	int index;
	bool end;
	RegexVMCode vc;
} Lexer;

typedef struct {
	char* reg;
	int tag;
} SymbolElement;

typedef struct {
	int num;
	char* str;
	int tag;
} Match;



Lexer compileLex(char_type* str,SymbolElement* el,int num);

bool nextMatch(Lexer* lex,Match* m);

void freeLex(Lexer* lex);




#endif // REGEX_VM
