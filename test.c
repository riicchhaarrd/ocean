#define HEAP_STRING_IMPL
#include "rhd/heap_string.h"

#define LINKED_LIST_IMPL
#include "rhd/linked_list.h"

#define HASH_MAP_IMPL
#include "rhd/hash_map.h"

#include "rhd/hash_string.h"

#include "ast.h"
#include "codegen.h"
#include "parse.h"
#include "token.h"
#include "arena.h"

void resolve_calls(ast_node_t* head, ast_node_t *func)
{
    traverse_context_t ctx = { 0 };

    ast_node_t* results[32];
    printf("%s\n", AST_NODE_TYPE_to_string(func->type));
    size_t n = ast_tree_nodes_by_type(&ctx, func, AST_FUNCTION_CALL_EXPR, &results, COUNT_OF(results));
    printf("numresults: %d\n", n);
    for (int i = 0; i < n; ++i)
    {
        ast_node_t* f = ast_tree_node_by_identifier(&ctx, head, results[i]->call_expr_data.callee->identifier_data.name, AST_FUNCTION_DECL);
        printf("\t%s\n", results[i]->call_expr_data.callee->identifier_data.name);
        if (f)
            resolve_calls(head, f);
    }
}

static int is_floating_point_type(int t)
{
    return t == LITERAL_FLOAT || t == LITERAL_DOUBLE;
}

static int coerce_type(int a, int b)
{
    if (a == b)
        return a;
    //both floating point return the bigger data type e.g double > float
    if (is_floating_point_type(a) && is_floating_point_type(b))
        return a == LITERAL_DOUBLE || b == LITERAL_DOUBLE ? LITERAL_DOUBLE : LITERAL_FLOAT;
    //coerce <float> + <int> to float
    //same for double
    if (is_floating_point_type(a))
        return a;
    return b;
}

static int ast_is_scope_node(ast_node_t* n)
{
    return n->type == AST_PROGRAM || n->type == AST_FUNCTION_DECL || n->type == AST_BLOCK_STMT || n->type == AST_DO_WHILE_STMT || n->type == AST_WHILE_STMT || n->type == AST_FOR_STMT || n->type == AST_IF_STMT;
}

//can either be AST_PROGRAM, AST_FUNCTION_DECL, AST_BLOCK
static ast_node_t *ast_find_scope_node(ast_node_t *head, ast_node_t* n)
{
    traverse_context_t ctx = { 0 };
    ast_tree_node_by_node(&ctx, head, n);
    //assumption that parent is always AST_PROGRAM
    for (int i = 1; i < ctx.numresults; ++i)
    {
        ast_node_t* it = ast_tree_traverse_get_visitee(&ctx, i);
        if (ast_is_scope_node(it))
            return it;
    }
    return NULL;
}

static ast_node_t *ast_node_expression_type(ast_node_t *head, ast_node_t* n)
{
    switch (n->type)
    {
    case AST_LITERAL:
    case AST_ENUM_VALUE:
        return n;
    case AST_EXPR_STMT:
        return ast_node_expression_type(head, n->expr_stmt_data.expr);
    case AST_IDENTIFIER:
    {
        //get current scope
        ast_node_t *scope = ast_find_scope_node(head, n);

        //find ident by name in tree
        traverse_context_t ctx = { 0 };
        ast_node_t* variable_decl = ast_tree_node_by_identifier(&ctx, head, n->identifier_data.name, AST_VARIABLE_DECL);
        if(variable_decl)
            return variable_decl->variable_decl_data.data_type;
        return NULL;
    } break;
    case AST_BIN_EXPR:
        return coerce_type(
            ast_node_expression_type(head, n->bin_expr_data.lhs),
            ast_node_expression_type(head, n->bin_expr_data.rhs)
        );
    case AST_ASSIGNMENT_EXPR:
        return coerce_type(
                ast_node_expression_type(head, n->assignment_expr_data.lhs),
                ast_node_expression_type(head, n->assignment_expr_data.rhs)
            );
    }
    return -1;
}

int main(int argc, char** argv)
{
	arena_t *arena;
	arena_create(&arena, "compiler", 1000 * 1000 * 32); //32MB
	
	compiler_t compile_ctx;
	compiler_init(&compile_ctx, arena, 64);
	
	ast_context_t ast_context;
	ast_init_context(&ast_context, arena);
	
    while (1)
    {
        //printf(">");
		const char *code = "int main(){ int test = 5; test += 10; return 0; }";
        //fgets(code, sizeof(code), stdin);
		if(code[0] == 'q')
			break;
        //printf("\n");
        struct token* tokens = NULL;
        int num_tokens = 0;

        //static const char* code = "int a = 3 + 3;";
        parse(code, &tokens, &num_tokens, LEX_FL_NONE);
		
		if(ast_process_tokens(&ast_context, tokens, num_tokens))
			break;
        free(tokens);
		
        traverse_context_t ctx = { 0 };
		void print_ast(struct ast_node *n, int depth);
		//print_ast(program_node, 0);

#if 0
        ast_node_t* main_func = ast_tree_node_by_identifier(&ctx, head, "main", AST_FUNCTION_DECL);
        if (!main_func)
        {
            printf("no main function!\n");
        }
        else
        {
            resolve_calls(head, main_func);
        }
#endif
		int codegen(compiler_t* ctx, ast_node_t*);
		if(codegen(&compile_ctx, ast_context.program_node))
			break;
		break;
    }
	printf("%d KB/%d KB bytes used\n", arena->used/1000, arena->reserved/1000);
	arena_destroy(&arena);
	return 0;
}