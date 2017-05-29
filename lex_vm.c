
#include <stdio.h>
#include <stdlib.h>
#include "lex_parse.h"
#include "lex_emit_code.h"
#include "lex_vm.h"



#define opcode_mask 0x0f
#define nlist_mask  0x40
#define clist_mask  0x80

#define OPCODE(pc)        (code[pc] & opcode_mask)
#define CHAR(pc,offset)   (*((char*)&code[pc+1] + offset))
#define ADDR(pc,offset)   (*((vm_addr_type*)&code[pc+1] + offset))

#define swap(type,a,b) { type t = a; a = b; b = t; }

#define addthread(list,c,pc) {       \
	if( !(code[pc] & list##_mask) ){ \
		list[c++] = pc;              \
		code[pc] |= list##_mask;     \
	}                                \
}

int topMatch(RegexVMCode vc,char* str,char** mSP){ // 先頭マッチによりマッチした文字列の直後のポインタを返す
	char* SP = str;
	*mSP = SP;
	vm_addr_type mPC = 0xffff;
	int tag = -1;
	vm_addr_type PC = 0;
	vm_code_type* code = vc.code;
	
	int nc = 0 , cc = 0 ;

#if USE_BUF_FLAG
	vm_addr_type* nlist = &vc.buf[ 0 ]; 
	vm_addr_type* clist = &vc.buf[ vc.opcode_size ]; 
#else
	vm_addr_type* nlist = (vm_addr_type*)malloc(sizeof(vm_addr_type)*vc.opcode_size);
	vm_addr_type* clist = (vm_addr_type*)malloc(sizeof(vm_addr_type)*vc.opcode_size);
#endif

	addthread(clist,cc,0);
	for(;;){
		for(int i = 0;i < cc;i++){
			PC = clist[i];
			switch(OPCODE(PC)){
			case VM_Char:
				if(*SP != CHAR(PC,0)) break;
				addthread(nlist,nc,PC+2);
				break;
			case VM_Range:
				if( ! ( CHAR(PC,0) <= *SP && *SP <= CHAR(PC,1) ) ) break;
				addthread(nlist,nc,PC+3);
				break;
			case VM_Any:
				addthread(nlist,nc,PC+1);
				break;
			case VM_NotChar:
				if(*SP == CHAR(PC,0)) break;
				addthread(clist,cc,PC+2);
				break;
			case VM_NotRange:
				if( CHAR(PC,0) <= *SP && *SP <= CHAR(PC,1) ) break;
				addthread(clist,cc,PC+3);
				break;
			case VM_Split:
				addthread(clist,cc,ADDR(PC,0));
				addthread(clist,cc,ADDR(PC,1));
				break;
			case VM_Jmp:
				addthread(clist,cc,ADDR(PC,0));
				break;
			case VM_Match:
				//printf("Match SP:%d PC:%d mPC:%d ",SP,PC,mPC);
				if(*mSP < SP || (*mSP == SP && PC < mPC) ){
					*mSP = SP;
					mPC = PC;
					tag = ADDR(PC,0);
					//printf("new tag : %d",tag);
				}
				//printf("\n");
				//printf("VM_Match : %s\n",mSP);
				break;
			}

		}

		// remove flag
		for(int i=0;i<cc;i++) code[clist[i]] &= ~clist_mask;

		// return result
		if(nc == 0) break;
		if(*SP == '\0'){
			printf("*SP == \\0\n");
			// remove flag
			for(int i=0;i<nc;i++) code[nlist[i]] &= ~nlist_mask;
			break;
		}

		// exchange flag
		for(int i=0;i<nc;i++){
			// nflag(at 7bit) -> cflag(at 8bit)	
			code[nlist[i]] &= ~nlist_mask;	
			code[nlist[i]] |=  clist_mask; 
		}

		
		swap(vm_addr_type*,nlist,clist);
		swap(int,nc,cc);
		//printf("%c [n:%d,c:%d]\n",*SP?*SP:'$',nc,cc);
	
		// clear nlist
		nc = 0;

		SP++;
	}

	

	
	// mSPはマッチした文字列の次の文字を指す
	//printf("SP: %s , mSP: %s \n",*SP?SP:"\\0",*mSP?mSP:"\\0");
	//printf("matched string : %d chars\n",*(unsigned int*)&mSP-*(unsigned int*)&iSP);
	//printf("thread_find : %d\n",thread_find);

#if USE_BUF_FLAG
#else
	free(nlist);
	free(clist);
#endif

	return tag;
}



Lexer compileLex(char_type* str,SymbolElement* el,int num){
	Lexer lex;
	lex.str = str;
	lex.index = 0;
	lex.end = false;

	RegexAST** asts = malloc(sizeof(RegexAST*)*num);
	for(int i=0;i<num;i++){
		asts[i] = parseRegex(&(el[i].reg));
	}

	/*for(int i=0;i<num;i++){
		printf("AST[%d] tag : %d\n",i,el[i].tag);
		printAST(asts[i],0);
		printf("\n");
	}*/
	
	lex.vc = emitVMCode(asts,el,num);

	//printVMCode(lex.vc);

	for(int i=0;i<num;i++){
		freeAST(asts[i]);
	}

	free(asts);

	return lex;
}

bool nextMatch(Lexer* lex,Match* m){
	if(lex->end){
		m->num = 0;
		m->str = NULL;
		m->tag = -2;
		return false;
	}

	m->str = &(lex->str[lex->index]);
	char* msp;
	m->tag = topMatch(lex->vc,m->str,&msp);
	m->num = msp - m->str;
	lex->index += m->num;
	lex->end = (*msp == '\0');
	return true;
}

void freeLex(Lexer* lex){
	lex->str = NULL;
	lex->index = 0;
	freeVMCode(lex->vc);
}



