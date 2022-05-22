#ifndef OPERAND_H
#define OPERAND_H
#include "imm.h"
#include "register.h"
#include <string.h>

typedef enum
{
	VOPERAND_INVALID,
	VOPERAND_IMMEDIATE,
	VOPERAND_INDIRECT,
	VOPERAND_REGISTER,
	VOPERAND_INDIRECT_REGISTER,
	VOPERAND_INDIRECT_REGISTER_DISPLACEMENT,
	VOPERAND_INDIRECT_REGISTER_INDEXED,
	// for dst only register and address are valid
	VOPERAND_LABEL
} voperand_type_t;

static const char *voperand_type_strings[] = {"invalid","immediate","indirect","register","indirect register","indirect register displacement","register indexed",NULL};

typedef enum
{
	VOPERAND_SIZE_NATIVE, // e.g native register size for the architecture (x86 -> 32, x64 -> 64),
	VOPERAND_SIZE_8_BITS = 1,
	VOPERAND_SIZE_16_BITS = 2,
	VOPERAND_SIZE_32_BITS = 4,
	VOPERAND_SIZE_64_BITS = 8,
	VOPERAND_SIZE_DOUBLE,
	VOPERAND_SIZE_FLOAT
} voperand_size_t;

static const char *voperand_size_names[] = {"native","","word","","dword","","","","qword","double","float",NULL};

typedef struct
{
	int type;
	voperand_size_t size;
	union
	{
		imm_t imm;
		vregister_t reg;
		imm_t indirect;
		struct
		{
			i32 disp;
			vregister_t reg;
		} reg_indirect_displacement;
		struct
		{
			i32 scale;
			vregister_t indexed_reg;
			vregister_t reg;
		} reg_indirect_indexed;
		size_t label;
	};
	bool virtual;
} voperand_t;

static voperand_t indirect_operand(imm_t imm, voperand_size_t size)
{
	voperand_t op = {.type = VOPERAND_INDIRECT, .size = size, .virtual = true};
	op.imm = imm;
	return op;
}

static voperand_t indirect_register_operand(vregister_t reg)
{
	voperand_t op = {.type = VOPERAND_INDIRECT_REGISTER, .size = VOPERAND_SIZE_NATIVE, .virtual = true};
	op.reg = reg;
	return op;
}

static voperand_t indirect_register_displacement_operand(vregister_t reg, u32 disp, voperand_size_t size)
{
	voperand_t op = {.type = VOPERAND_INDIRECT_REGISTER_DISPLACEMENT, .size = size, .virtual = true};
	op.reg_indirect_displacement.reg = reg;
	op.reg_indirect_displacement.disp = disp;
	return op;
}

static voperand_t indirect_register_indexed_operand(vregister_t reg, vregister_t indexreg, u32 scale, voperand_size_t size)
{
	voperand_t op = {.type = VOPERAND_INDIRECT_REGISTER_INDEXED, .size = size, .virtual = true};
	op.reg_indirect_indexed.indexed_reg = indexreg;
	op.reg_indirect_indexed.reg = reg;
	op.reg_indirect_indexed.scale = scale;
	return op;
}

static voperand_t label_operand(size_t label_value)
{
	voperand_t op = {.type = VOPERAND_LABEL, .size = VOPERAND_SIZE_NATIVE, .virtual = true};
	op.label = label_value;
	return op;
}

static voperand_t imm32_operand(i32 i)
{
	voperand_t op = {.type = VOPERAND_IMMEDIATE, .size = VOPERAND_SIZE_32_BITS, .virtual = true};
	op.imm.nbits = 32;
	op.imm.dd = i;
	return op;
}

static voperand_t imm64_operand(i64 i)
{
	voperand_t op = {.type = VOPERAND_IMMEDIATE, .size = VOPERAND_SIZE_64_BITS, .virtual = true};
	op.imm.nbits = 64;
	op.imm.dq = i;
	return op;
}

static voperand_t invalid_operand()
{
	voperand_t op = {.type = VOPERAND_INVALID, .virtual = true};
	return op;
}

static voperand_t register_operand(vregister_t reg)
{
	voperand_t op = {.type = VOPERAND_REGISTER, .size = VOPERAND_SIZE_NATIVE, .virtual = true};
	op.reg = reg;
	return op;
}

static bool voperand_type_equal(voperand_t* a, voperand_t* b)
{
	if (a->type == VOPERAND_REGISTER && b->type == VOPERAND_REGISTER && a->size == b->size)
		return true;
	return !memcmp(a, b, sizeof(voperand_t));
}

#endif
