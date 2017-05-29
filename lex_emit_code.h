#ifndef VM_EMIT_CODE
#define VM_EMIT_CODE

#include "lex_vm.h"


// バイトコードオペコード
#define VM_Match    0
#define VM_Any      1
#define VM_Char     2
#define VM_NotChar  3
#define VM_Range    4
#define VM_NotRange 5
#define VM_Split    6
#define VM_Jmp      7

/*
const vm_code_type VM_Match    = 0;
const vm_code_type VM_Any      = 1;
const vm_code_type VM_Char     = 2;
const vm_code_type VM_NotChar  = 3;
const vm_code_type VM_Range    = 4;
const vm_code_type VM_NotRange = 5;
const vm_code_type VM_Split    = 6;
const vm_code_type VM_Jmp      = 7;
*/



struct RegexAST;


RegexVMCode emitVMCode(RegexAST** ast,SymbolElement* el,int num);

void freeVMCode(RegexVMCode vc);

void printVMCode(RegexVMCode vc);


#endif // VM_EMIT_CODE
