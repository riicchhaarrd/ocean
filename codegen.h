#ifndef CODEGEN_H
#define CODEGEN_H

#include "compile.h"
#include "ast.h"
#include <setjmp.h>

#define COUNT_OF(x) (sizeof((x)) / sizeof((x)[0]))

typedef int (*traversal_fn_t)(ast_node_t*, void*);

typedef struct
{
	jmp_buf jmp;
	traversal_fn_t visitor;
	void* userdata;
	size_t visiteestacksize;
	ast_node_t* visiteestack[8];
	int single_result;
	int overflow;
	ast_node_t **results;
	size_t maxresults, numresults;
} traverse_context_t;

typedef struct
{
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
} codegen_t;

ast_node_t* ast_tree_traverse(traverse_context_t* ctx, ast_node_t* head, traversal_fn_t visitor, void* userdata);
ast_node_t* ast_tree_node_by_type(traverse_context_t* ctx, ast_node_t* head, int type);
ast_node_t* ast_tree_node_by_identifier(traverse_context_t* ctx, ast_node_t* head, const char* id, int type);
ast_node_t* ast_tree_traverse_get_visitee(traverse_context_t* ctx, size_t index);
size_t ast_tree_nodes_by_type(traverse_context_t* ctx, ast_node_t* head, int type, ast_node_t** results, size_t maxresults);
ast_node_t* ast_tree_node_by_node(traverse_context_t* ctx, ast_node_t* head, ast_node_t* node);

#endif