#include "compile.h"
#include "imm.h"
#include "operand.h"
#include "virtual_opcodes.h"
#include "util.h"

static int32_t voperand_cast_i32(voperand_t *o)
{
	assert(o->type == VOPERAND_IMMEDIATE);
	return imm_cast_int32_t(&o->imm);
}

typedef enum
{
	EAX,
	ECX,
	EDX,
	EBX,
	ESP,
	EBP,
	ESI,
	EDI
} X86_REGISTER;

typedef enum
{
	PUSH32r,
	PUSH32i8,
	PUSH32i32,
	PUSH32rmm
} x86_instr_t;

void add(heap_string *s, voperand_t *dst, voperand_t *src)
{
	// TODO: build table
	//  https://qbdi.readthedocs.io/en/stable/architecture_support.html
	//  of e.g all the ADD specific instructions then match them by their operands (maybe fix VOPERAND types and add
	//  float/double to that aswell, since it's based on size for now) then just loop through the mappings e.g
	// static map_t mappings[] = {{ADD32ri, VOPERAND_REGISTER, VOPERAND_IMMEDIATE32/VOPERAND_IMMEDIATE8};
	switch(dst->type)
	{
	case VOPERAND_REGISTER:
		//TODO:
		break;
	}
}

void push(heap_string *s, voperand_t *op)
{
	switch(op->type)
	{
	case VOPERAND_REGISTER:
		db(s, 0x50 + op->reg.index);
		break;
	case VOPERAND_IMMEDIATE:
		db(s, 0x68);
		dd(s, voperand_cast_i32(op));
		break;
	default:
		perror("unhandled");
		break;
	}
}

void mov(heap_string *s, voperand_t *dst, voperand_t *src)
{
}
void div(heap_string *s, voperand_t *dst, voperand_t *src)
{
}
void mul(heap_string *s, voperand_t *dst, voperand_t *src)
{
}
void sub(heap_string *s, voperand_t *dst, voperand_t *src)
{
}

bool x86(function_t *f, heap_string *s)
{
	for(size_t i = 0; i < f->instruction_index; ++i)
	{
		vinstr_t* instr = &f->instructions[i];
		voperand_t* op = &instr->operands[0];
		switch(instr->opcode)
		{
			case VOP_CALL:
				db(s, 0xe8);
				dd(s, 0x0); // TODO: replace
				break;

			case VOP_SUB:
				assert(instr->numoperands == 2);
				sub(s, &instr->operands[0], &instr->operands[1]);
				break;
			case VOP_MUL:
				assert(instr->numoperands == 2);
				mul(s, &instr->operands[0], &instr->operands[1]);
				break;
			case VOP_DIV:
				assert(instr->numoperands == 2);
				div(s, &instr->operands[0], &instr->operands[1]);
				break;
			case VOP_ADD:
				assert(instr->numoperands == 2);
				add(s, &instr->operands[0], &instr->operands[1]);
				break;

			case VOP_PUSH:
				push(s, op);
			break;
			
			case VOP_ALLOCA:
			{
				i32 numbytes = voperand_cast_i32(&instr->operands[0]);
				numbytes += 16 - (numbytes % 16);
				db(s, 0x81);
				db(s, 0xec);
				dd(s, numbytes);
			}
			break;
			
			case VOP_ENTER:
				db(s, 0x55); // push ebp
				db(s, 0x89); // mov ebp, esp
				db(s, 0xe5);
				break;
			case VOP_LEAVE:
				db(s, 0x5d); // pop ebp
				db(s, 0x89); // mov esp, ebp
				db(s, 0xec);
				break;

			case VOP_RET:
				break;

			case VOP_LABEL:
				break;

			case VOP_JMP:
				break;

			case VOP_MOV:
			{
				assert(instr->numoperands == 2);
				mov(s, &instr->operands[0], &instr->operands[1]);
			} break;

			default:
				printf("unhandled opcode %s", vopcode_names[instr->opcode]);
				return false;
		}
	}
	return true;
}
