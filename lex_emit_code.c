/*

// regex vm operator

// register
PC      // プログラムカウンタ(スレッドごとに存在する)
SP      // ストリングポインタ、解析中の文字列の位置(スレッドごとに存在する)

// operator

char a           // SPと比較して、SP==aならSPとPCをすすめる。
                 // 違っていたら、スレッドを終了し失敗とする。

range a,z        // char a の範囲版

any              // char a の任意文字版

notchar a        // char a の否定版、SP==aならばスレッドを終了する、SP!=aならばPCをすすめる。(SPはすすめない)

notrange a,z     // range a,z の否定版、notcharと同様

split L1,L2      // スレッドを分割する。
                 //SPをコピーし、PC=L2のスレッドを作る。現在実行中のスレッドはPC＝L1に設定する。

jmp L            // ラベルL(アドレス)にジャンプする。PC=L にセットする。

match            // スレッドを終了し、成功とする。


// AST -> codes

a      ||   char a

a-z    ||   range a,z

.      ||   any

[^abc] ||   notchar a
       ||   notchar b
	   ||   notchar c
       ||   any

[^a-z] ||   notrange a,z
       ||   any

EF     ||   codes for E
       ||   codes for F

E|F    ||   split L1,L2
       || L1: codes for E
       ||   jmp L3
       || L2: codes for F
       || L3:

E?     ||   split L1,L2
       || L1: codes for E
       || L2: 

E*     || L1: split L2,L3
       || L2: codes for E
       ||   jmp L1
       || L3:

E+     || L1: codes for E
       ||   split L1,L2
       || L2:



opcode : vm_code_type <unsigned char>
0-3:命令用
4,5:特になし
6,7:clist、nlistに追加されているかどうかのフラグ
|7|6|5|4| 3 | 2 | 1 | 0 | 
|c|n| | |     opcode    |

*/


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "lex_parse.h"
#include "lex_emit_code.h"




// AST->VMcode を実行する関数群

#define genChar(c)       genCharOrNotChar(true, c)
#define genNotChar(c)    genCharOrNotChar(false,c)

#define genRange(b,e)    genRangeOrNotRange(true, b,e)
#define genNotRange(b,e) genRangeOrNotRange(false,b,e)


static vm_code_type* code_top;
static vm_addr_type  code_size;
static size_t        opcode_size;
static size_t        code_alloced_size;

static void initGenCode(void){
	code_size = 0;
	opcode_size = 0;
	code_alloced_size = 128;
	code_top = (vm_code_type*)malloc(code_alloced_size*sizeof(vm_code_type));
}

void freeVMCode(RegexVMCode vc){
	free(vc.code);
	free(vc.buf);
}

static vm_addr_type getCodeCount(void){ // 今までに発行した命令数、次の命令は配列の返り値番目に格納される
	if(code_size + 10 >= code_alloced_size){
		//code_alloced_size += 100;
		code_alloced_size = code_alloced_size * 3 / 2;
		code_top = (vm_code_type*)realloc(code_top,code_alloced_size*sizeof(vm_code_type));
		//printf("expand! code_alloced_size=%zu\n",code_alloced_size);
	}
	return code_size;
}

static inline vm_addr_type genMatch(int tag){
	vm_addr_type count = getCodeCount();
	code_top[count] = VM_Match;
	*(vm_addr_type*)&code_top[count+1] = (vm_addr_type)tag;
	code_size += sizeof(vm_code_type) + sizeof(vm_addr_type);
	opcode_size++;
	return count;
}

static inline void genAny(void){
	code_top[getCodeCount()] = VM_Any;
	code_size++;
	opcode_size++;
}

static inline void genCharOrNotChar(bool op,char_type c){
	vm_addr_type count = getCodeCount();
	code_top[count] = op?VM_Char:VM_NotChar;
	*(char_type*)&(code_top[count+1]) = c;
	code_size += sizeof(vm_code_type) + sizeof(char_type);
	opcode_size++;
}

static inline void genRangeOrNotRange(bool op,char_type b,char_type e){
	vm_addr_type count = getCodeCount();
	code_top[count] = op?VM_Range:VM_NotRange;
	*(char_type*)&code_top[count+1] = b;
	*((char_type*)&code_top[count+1] + 1) = e;
	code_size += sizeof(vm_code_type) + sizeof(char_type)*2;
	opcode_size++;
}

static inline vm_addr_type genSplit(vm_addr_type l1,vm_addr_type l2){
	vm_addr_type count = getCodeCount();
	code_top[count] = VM_Split;
	*(vm_addr_type*)&code_top[count+1] = l1;
	*((vm_addr_type*)&code_top[count+1] + 1) = l2;
	code_size += sizeof(vm_code_type) + sizeof(vm_addr_type)*2;
	opcode_size++;
	return count;
}

static inline vm_addr_type genJmp(vm_addr_type l){
	vm_addr_type count = getCodeCount();
	code_top[count] = VM_Jmp;
	*(vm_addr_type*)&code_top[count+1] = l;
	code_size += sizeof(vm_code_type) + sizeof(vm_addr_type);
	opcode_size++;
	return count;
}

static void patchSplitL1(vm_addr_type s,vm_addr_type l1){
	*(vm_addr_type*)&code_top[s+1] = l1;
}

static void patchSplitL2(vm_addr_type s,vm_addr_type l2){
	*((vm_addr_type*)&code_top[s+1] + 1) = l2;
}

static void patchJmp(vm_addr_type j,vm_addr_type l){
	*(vm_addr_type*)&code_top[j+1] = l;
}

static void convertASTtoCode(RegexAST* ast,bool state){
	if(ast == NULL) return;
	if(state == false){ // てきとー [^ ] の中身の命令
		switch(ast->type){
			case Char:    genNotChar(ast->c); return;
			case Range:   genNotRange(ast->begin,ast->end); return;
			case Dot:     printf("[^.] is not supported."); return;
			case Or: 
				convertASTtoCode(ast->lhs,false);
				convertASTtoCode(ast->rhs,false);
				return;
			default:
				printf("error [^ ] only char,range,or.");
				return;
		}
	}
	switch(ast->type){
		case Char:    genChar(ast->c); return;
		case Range:   genRange(ast->begin,ast->end); return;
		case Dot:     genAny(); return;
		case Connect: convertASTtoCode(ast->lhs,true); convertASTtoCode(ast->rhs,true); return;
		case Or:{
			vm_addr_type ls = genSplit(0,0);
			vm_addr_type l1 = getCodeCount();
			convertASTtoCode(ast->lhs,true);
			vm_addr_type lj = genJmp(0);
			vm_addr_type l2 = getCodeCount();
			convertASTtoCode(ast->rhs,true);
			vm_addr_type l3 = getCodeCount();
			patchSplitL1(ls,l1);
			patchSplitL2(ls,l2);
			patchJmp(lj,l3);
			return;
		}
		case Star:{
			vm_addr_type l1 = getCodeCount();
			vm_addr_type ls = genSplit(0,0);
			vm_addr_type l2 = getCodeCount();
			convertASTtoCode(ast->lhs,true);
			vm_addr_type lj = genJmp(0);
			vm_addr_type l3 = getCodeCount();
			patchJmp(lj,l1);
			patchSplitL1(ls,l2);
			patchSplitL2(ls,l3);
			return;
		}
		case Question:{
			vm_addr_type ls = genSplit(0,0);
			vm_addr_type l1 = getCodeCount();
			convertASTtoCode(ast->lhs,true);
			vm_addr_type l2 = getCodeCount();
			patchSplitL1(ls,l1);
			patchSplitL2(ls,l2);
			return;
		}
		case Plus:{
			vm_addr_type l1=getCodeCount() ,ls;
			convertASTtoCode(ast->lhs,true);
			ls = genSplit(l1,0);
			patchSplitL2(ls,getCodeCount());
			return;
		} 
		case Not:
			convertASTtoCode(ast->lhs,false);
			genAny();
			return;
	}
}

RegexVMCode emitVMCode(RegexAST** ast,SymbolElement* el,int n){
	vm_addr_type Lsplit,Lcode,Lnextsplit;
	initGenCode();
	
	for(int i=0;i<n-1;i++){
		Lsplit = genSplit(0,0);
		Lcode  = getCodeCount();
		convertASTtoCode(ast[i],true);
		genMatch(el[i].tag);
		Lnextsplit = getCodeCount();
		patchSplitL1(Lsplit,Lcode);
		patchSplitL2(Lsplit,Lnextsplit);
	}

	convertASTtoCode(ast[n-1],true);
	genMatch(el[n-1].tag);


	code_top = realloc(code_top,code_size*sizeof(vm_code_type));
	//printf("shurink! code_size=%d,opcode_size=%zu\n",code_size,opcode_size);
	//printf("reallocated! %lubytes + %lubytes = %lubytes.\n",code_size*sizeof(vm_code_type),2*opcode_size*sizeof(vm_addr_type),code_size*sizeof(vm_code_type)+2*opcode_size*sizeof(vm_addr_type));


#if USE_BUF_FLAG
	vm_addr_type* buf = malloc(2*opcode_size*sizeof(vm_addr_type));
#else
	vm_addr_type* buf = NULL;
#endif	

	RegexVMCode vc={code_size,opcode_size,code_top,buf};
	return vc;
}


void printVMCode(RegexVMCode vc){
	printf("code_size : %zu , opcode_size : %zu \n",vc.code_size,vc.opcode_size);
	vm_code_type* code = vc.code;
	for(vm_addr_type PC = 0;PC < vc.code_size;){
		printf("%04d : ",PC);
		switch(code[PC]){
			case VM_Char:
				printf("char %c",*(char*)&code[PC+1]);
				PC += 2;
				break;
			case VM_Range:
				printf("range %c , %c",*(char*)&code[PC+1],*(char*)&code[PC+2]);
				PC += 3;
				break;
			case VM_Any:
				printf("any .");
				PC += 1;
				break;
			case VM_NotChar:
				printf("not char %c",*(char*)&code[PC+1]);
				PC += 2;
				break;
			case VM_NotRange:
				printf("not range %c , %c",*(char*)&code[PC+1],*(char*)&code[PC+2]);
				PC += 3;
				break;
			case VM_Split:
				printf("split %04d , %04d",*(vm_addr_type*)&code[PC+1],*((vm_addr_type*)&code[PC+1] + 1));
				PC += 1 + sizeof(vm_addr_type)*2;
				break;
			case VM_Jmp:
				printf("jmp %04d",*(vm_addr_type*)&code[PC+1]);
				PC += 1 + sizeof(vm_addr_type);
				break;
			case VM_Match:
				printf("match %d",*(vm_addr_type*)&code[PC+1]);
				PC += 1 + sizeof(vm_addr_type);
				break;
			default:
				printf("!!!unknown opecode!!! code[PC] = 0x%x",code[PC]);
				goto L;
		}
		printf("\n");
	}

	return;
L:
	printf("print Error!\n");
	return;
}



