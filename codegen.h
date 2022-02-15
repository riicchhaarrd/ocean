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

typedef enum
{
	REG1,
	REG2,
	REG3,
	REG4
} cg_register_t;

typedef struct
{
	cg_register_t (*add)(compiler_t* ctx, cg_register_t a, cg_register_t b);
	cg_register_t (*sub)(compiler_t* ctx, cg_register_t a, cg_register_t b);
	cg_register_t (*div)(compiler_t* ctx, cg_register_t a, cg_register_t b);
	cg_register_t (*mul)(compiler_t* ctx, cg_register_t a, cg_register_t b);
} codegen_t;

ast_node_t* ast_tree_traverse(traverse_context_t* ctx, ast_node_t* head, traversal_fn_t visitor, void* userdata);
ast_node_t* ast_tree_node_by_type(traverse_context_t* ctx, ast_node_t* head, int type);
ast_node_t* ast_tree_node_by_identifier(traverse_context_t* ctx, ast_node_t* head, const char* id, int type);
ast_node_t* ast_tree_traverse_get_visitee(traverse_context_t* ctx, size_t index);
size_t ast_tree_nodes_by_type(traverse_context_t* ctx, ast_node_t* head, int type, ast_node_t** results, size_t maxresults);
ast_node_t* ast_tree_node_by_node(traverse_context_t* ctx, ast_node_t* head, ast_node_t* node);

#endif