#include "codegen.h"
#include "rhd/linked_list.h"

static void visit_ast_node(traverse_context_t *ctx, ast_node_t *n)
{
    if (ctx->single_result)
    {
        ctx->visiteestacksize = (ctx->visiteestacksize + 1) % COUNT_OF(ctx->visiteestack);
        ctx->visiteestack[ctx->visiteestacksize] = n;
    }
    //printf("n=%s\n", AST_NODE_TYPE_to_string(n->type));
    if (ctx->visitor(n, ctx->userdata))
    {
        if (ctx->single_result)
        {
            longjmp(ctx->jmp, 1);
        }
        else
        {
            if (ctx->numresults + 1 >= ctx->maxresults)
            {
                ctx->overflow = 1;
                longjmp(ctx->jmp, 1);
            }
            ctx->results[ctx->numresults++] = n;
        }
    }
}

static void traverse_node(traverse_context_t *ctx, ast_node_t* n)
{
    visit_ast_node(ctx, n);
    switch (n->type)
    {
    case AST_PROGRAM:
        linked_list_reversed_foreach(n->program_data.body, ast_node_t**, it,
        {
            traverse_node(ctx, *it);
        });
        break;
    case AST_BLOCK_STMT:
        linked_list_reversed_foreach(n->block_stmt_data.body, ast_node_t**, it,
        {
            traverse_node(ctx, *it);
        });
        break;

    case AST_VARIABLE_DECL:
        traverse_node(ctx, n->variable_decl_data.data_type);
        traverse_node(ctx, n->variable_decl_data.id);
        traverse_node(ctx, n->variable_decl_data.initializer_value);
        break;

    case AST_FUNCTION_DECL:
    {
        traverse_node(ctx, n->func_decl_data.id);
        for(int i = 0; i < n->func_decl_data.numparms; ++i)
            traverse_node(ctx, n->func_decl_data.parameters[i]);
        traverse_node(ctx, n->func_decl_data.body);
        for (int i = 0; i < n->func_decl_data.numdeclarations; ++i)
            traverse_node(ctx, n->func_decl_data.declarations[i]);
        traverse_node(ctx, n->func_decl_data.return_data_type); //TODO: maybe put these into their own node type
    } break;

    case AST_FOR_STMT:
        traverse_node(ctx, n->for_stmt_data.body);
        traverse_node(ctx, n->for_stmt_data.init);
        traverse_node(ctx, n->for_stmt_data.test);
        traverse_node(ctx, n->for_stmt_data.update);
        break;

    case AST_WHILE_STMT:
        traverse_node(ctx, n->while_stmt_data.body);
        traverse_node(ctx, n->while_stmt_data.test);
        break;

    case AST_RETURN_STMT:
        traverse_node(ctx, n->return_stmt_data.argument);
        break;
    }
}

ast_node_t* ast_tree_traverse_get_visitee(traverse_context_t *ctx, size_t index)
{
    size_t max = COUNT_OF(ctx->visiteestack);
    if (index >= max)
        return NULL;
    return ctx->visiteestack[(ctx->visiteestacksize - index) % max];
}

ast_node_t *ast_tree_traverse(traverse_context_t *ctx, ast_node_t *head, traversal_fn_t visitor, void *userdata)
{
    if (!head)
        return NULL;
    ctx->visitor = visitor;
    ctx->userdata = userdata;
    if (setjmp(ctx->jmp))
    {
        if (ctx->overflow)
            return NULL;
        return ast_tree_traverse_get_visitee(ctx, 0);
    }
    traverse_node(ctx, head);
    return NULL;
}

static int ast_filter_type(ast_node_t* n, int* ptype)
{
    if (n->type == *ptype)
        return 1;
    return 0;
}

static int ast_filter_identifier(ast_node_t* n, const char** id)
{
    if (n->type == AST_IDENTIFIER && !strcmp(n->identifier_data.name, *id))
        return 1;
    return 0;
}

ast_node_t* ast_tree_node_by_type(traverse_context_t* ctx, ast_node_t* head, int type)
{
    ctx->single_result = 1;
    return ast_tree_traverse(ctx, head, ast_filter_type, &type);
}

size_t ast_tree_nodes_by_type(traverse_context_t* ctx, ast_node_t* head, int type, ast_node_t **results, size_t maxresults)
{
    ctx->single_result = 0;
    ctx->results = results;
    ctx->maxresults = maxresults;
    ctx->numresults = 0;
    ast_tree_traverse(ctx, head, ast_filter_type, &type);
    return ctx->numresults;
}

ast_node_t* ast_tree_node_by_identifier(traverse_context_t* ctx, ast_node_t* head, const char *id, int type)
{
    ctx->single_result = 1;
    if (!ast_tree_traverse(ctx, head, ast_filter_identifier, &id))
        return NULL;
    for (int i = 1; i < COUNT_OF(ctx->visiteestack); ++i)
    {
        ast_node_t *n = ast_tree_traverse_get_visitee(ctx, i);
        if (!n || type == AST_NONE || n->type == type)
            return n;
    }
    return NULL;
}

int process(compiler_t* ctx, ast_node_t* n)
{
    switch (n->type)
    {
        case AST_BIN_EXPR:
        {
            struct ast_node* lhs = n->bin_expr_data.lhs;
            struct ast_node* rhs = n->bin_expr_data.rhs;
        } break;
    }
}

int codegen(compiler_t* ctx, ast_node_t *head)
{
    process(ctx, head);
    return 0;
}
