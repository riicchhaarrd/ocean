#include "ast.h"
#include "codegen.h"
#include "parse.h"
#include "token.h"

#include "rhd/heap_string.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"
#include "rhd/hash_string.h"

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

int main(int argc, char** argv)
{
    struct token* tokens = NULL;
    int num_tokens = 0;

    static const char *code = "void test(){test3();}int main(){int a = 3;test();return 0;}";
    parse(code, &tokens, &num_tokens, LEX_FL_NONE);

    struct linked_list* ast_list = NULL;
    int verbose = 0;
    ast_node_t* head = NULL;
    int ast = generate_ast(tokens, num_tokens, &ast_list, &head, verbose);
    traverse_context_t ctx = { 0 };

    ast_node_t * main_func = ast_tree_node_by_identifier(&ctx, head, "main", AST_FUNCTION_DECL);
    if (!main_func)
    {
        printf("no main function!\n");
    }
    else
    {
        resolve_calls(head, main_func);
    }

    linked_list_destroy(&ast_list);
    free(tokens);
	return 0;
}