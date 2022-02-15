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

static void process_ast_node(ast_node_t* n)
{
    switch (n->type)
    {
        case AST_BIN_EXPR:
        {
            struct ast_node* lhs = n->bin_expr_data.lhs;
            struct ast_node* rhs = n->bin_expr_data.rhs;
            //float > int
            if(lhs->)
        } break;
    }
}

int main(int argc, char** argv)
{
    while (1)
    {
        printf(">");
        char code[1024];
        fgets(code, sizeof(code), stdin);
        printf("\n");
        struct token* tokens = NULL;
        int num_tokens = 0;

        //static const char* code = "int a = 3 + 3;";
        parse(code, &tokens, &num_tokens, LEX_FL_NONE);

        struct linked_list* ast_list = NULL;
        int verbose = 0;
        ast_node_t* head = NULL;
        int ast = generate_ast(tokens, num_tokens, &ast_list, &head, verbose);
        traverse_context_t ctx = { 0 };

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

        process_ast_node(head);

        linked_list_destroy(&ast_list);
        free(tokens);
    }
	return 0;
}