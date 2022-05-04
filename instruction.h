#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "imm.h"
#include "operand.h"
#include "virtual_opcodes.h"

typedef struct
{
	size_t index;
	vopcode_t opcode;
	voperand_t operands[4];
	size_t numoperands;
} vinstr_t;

#endif
