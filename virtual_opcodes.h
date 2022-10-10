#ifndef VIRTUAL_OPCODES_H
#define VIRTUAL_OPCODES_H
#include <stdbool.h>

static const char* vopcode_names[] = {"add",  "sub",		 "mul",	   "div",	 "mod",	  "fadd",	"fsub",	 "fmul",
									  "fdiv", "fmod",		 "sitofp", "fptosi", "and",	  "or",		"xor",	 "not",
									  "mov",  "load",		 "lea",	   "store",	 "push",  "pop",	"enter", "leave",
									  "call", "call extern", "ret",	   "test",	 "cmp",	  "jmp",	"jnz",	 "jz",
									  "jle",  "jge",		 "jg",	   "jl",	 "label", "alloca", "hlt",	 NULL};

typedef enum
{	
	VOP_ADD,
	VOP_SUB,
	VOP_MUL,
	VOP_DIV,
	VOP_MOD,

	VOP_FADD,
	VOP_FSUB,
	VOP_FMUL,
	VOP_FDIV,
	VOP_FMOD,

	VOP_SITOFP,
	VOP_FPTOSI,

	VOP_AND,
	VOP_OR,
	VOP_XOR,
	VOP_NOT,
	
	VOP_MOV,
	VOP_LOAD,
	VOP_LEA,
	
	VOP_STORE,

	VOP_PUSH,
	VOP_POP,
	
	VOP_ENTER,
	VOP_LEAVE,

	VOP_CALL,
	VOP_CALL_EXTERN,
	VOP_RET,

	VOP_TEST,
	VOP_CMP,

	VOP_JMP,
	VOP_JNZ,
	VOP_JZ,
	VOP_JLE,
	VOP_JGE,
	VOP_JG,
	VOP_JL,
	VOP_LABEL,
	VOP_ALLOCA,
	VOP_HLT
} vopcode_t;

static bool vopcode_overwrites_first_operand(vopcode_t op)
{
	return op <= VOP_LEA;
}

#endif
