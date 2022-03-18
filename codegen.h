#ifndef CODEGEN_H
#define CODEGEN_H

typedef enum vreg_s vreg_t;
typedef int reg_t;
typedef struct compiler_s compiler_t;
typedef struct reljmp_s reljmp_t;

typedef struct reginfo_s
{
	const char *name;
	int id;
	int bits;
	int slot;
	//TODO: FIXME should probably later be a stack of vreg values
	int usecount;
} reginfo_t;

typedef struct lvalue_s
{
	int offset_type; //e.g global variable memory address, stack offset
	int offset;
	struct ast_node *data_type;
	int size;
} lvalue_t;

struct codegen_s
{
	reginfo_t *reginfo;
	size_t numreginfo;
	
	reg_t (*map_register)(compiler_t *ctx, vreg_t);
	void (*unmap_register)(compiler_t *ctx, reg_t);
	const char *(*register_name)(compiler_t *ctx, reg_t);
	
	//----------------------------------------
	reg_t (*add)(compiler_t* ctx, reg_t a, reg_t b);
	reg_t (*sub)(compiler_t* ctx, reg_t a, reg_t b);
	reg_t (*mod)(compiler_t* ctx, reg_t a, reg_t b);
	reg_t (*imul)(compiler_t* ctx, reg_t reg);
	reg_t (*idiv)(compiler_t* ctx, reg_t reg);
	reg_t (*add_imm8_to_r32)(compiler_t* ctx, reg_t a, u8 value);
	reg_t (*add_imm32_to_r32)(compiler_t* ctx, reg_t a, u32 value);
	reg_t (*inc)(compiler_t* ctx, reg_t reg);
	reg_t (*neg)(compiler_t* ctx, reg_t reg);
	reg_t (*sub_regn_imm32)(compiler_t* ctx, reg_t reg, i32 imm);

	//----------------------------------------
	reg_t (*xor)(compiler_t* ctx, reg_t a, reg_t b);
	reg_t (*and)(compiler_t* ctx, reg_t a, reg_t b);
	reg_t (*or)(compiler_t* ctx, reg_t a, reg_t b);

	//----------------------------------------
	void (*int3)(compiler_t *ctx);
	void (*nop)(compiler_t* ctx);
	void (*invoke_syscall)(compiler_t* ctx, struct ast_node** args, int numargs);
	void (*exit_instr)(compiler_t* ctx, reg_t reg);
	//void (*int_imm8)(compiler_t *ctx, u8 value); //don't expose directly, just use invoke_syscall and other functions

	//----------------------------------------
	void (*push)(compiler_t *ctx, reg_t reg);
	void (*pop)(compiler_t *ctx, reg_t reg);
	void (*load_reg)(compiler_t* ctx, reg_t a, reg_t b);
	void (*store_reg)(compiler_t* ctx, reg_t a, reg_t b);
	void (*load_regn_base_offset_imm32)(compiler_t* ctx, reg_t reg, i32 imm);

	//----------------------------------------
	void (*ret)(compiler_t* ctx);
	int (*indirect_call_imm32)(compiler_t* ctx, intptr_t loc, int* address_loc);
	void (*call_imm32)(compiler_t* ctx, int loc);
	void (*call_r32)(compiler_t* ctx, reg_t reg);

	//----------------------------------------
	void (*mov_r_imm32)(compiler_t* ctx, reg_t reg, i32 imm, int* data_loc);
	void (*mov_r_string)(compiler_t* ctx, reg_t reg, const char* str);
	reg_t(*mov)(compiler_t* ctx, reg_t a, reg_t b);

	//----------------------------------------
	void (*cmp)(compiler_t* ctx, reg_t a, reg_t b);
	void (*test)(compiler_t* ctx, reg_t a, reg_t b);
	int (*if_beg)(compiler_t* ctx, reg_t a, int operator, reg_t b, int* offset);
	int (*if_else)(compiler_t* ctx, int* offset);
	int (*if_end)(compiler_t* ctx, int* offset);
	void (*jmp_begin)(compiler_t* ctx, reljmp_t* jmp, int type);
	void (*jmp_end)(compiler_t* ctx, reljmp_t* jmp);

	//----------------------------------------
	int (*add_data)(compiler_t* ctx, void* data, u32 data_size);
	
	void (*load_offset_from_stack_to_register)(compiler_t *, reg_t reg, int offset, int data_size);
	void (*store_offset_from_register_to_stack)(compiler_t *, reg_t reg, int offset, int data_size);
};

typedef struct codegen_s codegen_t;

#endif