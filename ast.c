#include "ast.h"
#include "token.h"
#include "rhd/linked_list.h"

struct ast_context
{
    struct token *tokens;
    int num_tokens;
    int token_index;

    struct linked_list *nodelist;
    
    struct ast_node *head;
    struct ast_node *root;

    struct token *current_token;

    bool verbose;
};

static int accept(struct ast_context *ctx, int type)
{
    struct token *tk = &ctx->tokens[ctx->token_index];
    ctx->current_token = tk;
    if(tk->type != type)
    {
        //ctx->current_token = NULL;
        return 1;
    }
    ctx->current_token = tk;
    ++ctx->token_index;
    return 0;
}

static int expect(struct ast_context *ctx, int type)
{
    if(accept(ctx, type))
    {
        printf("expected type %d, got %d\n", type, ctx->tokens[ctx->token_index]);
        return 1;
    }
    return 0;
}

static int ast_error(struct ast_context *ctx, const char *errmessage)
{
    printf("error: %s\n", errmessage);
    return 1;
}

static struct ast_node *push_node(struct ast_context *ctx, int type)
{
    //temporarily store current head
    struct ast_node *head = ctx->head;

	struct ast_node t = {
            .parent = head,
            .type = type
    };
    
    struct ast_node *node = linked_list_prepend(ctx->nodelist, t);
    
    ctx->head = node;
    
    return node;
}

static void pop_node(struct ast_context *ctx)
{
    //set head to parent of current head
    assert(ctx->head->parent == NULL && ctx->head != ctx->root);
    if(ctx->head == ctx->root)
        return;
    ctx->head = ctx->head->parent;
}

static struct ast_node *unary_expr(struct ast_context *ctx, int op, bool prefix, struct ast_node *arg)
{
    struct ast_node *n = push_node(ctx, AST_UNARY_EXPR);
    n->unary_expr_data.operator = op;
    n->unary_expr_data.prefix = prefix;
    n->unary_expr_data.argument = arg;
    return n;
}

static struct ast_node *bin_expr(struct ast_context *ctx, int op, struct ast_node *lhs, struct ast_node *rhs)
{
    struct ast_node *n = push_node(ctx, AST_BIN_EXPR);
    n->bin_expr_data.operator = op;
    n->bin_expr_data.lhs = lhs;
    n->bin_expr_data.rhs = rhs;
    return n;
}

static struct ast_node *assignment_expr(struct ast_context *ctx, int op, struct ast_node *lhs, struct ast_node *rhs)
{
    struct ast_node *n = push_node(ctx, AST_ASSIGNMENT_EXPR);
    n->bin_expr_data.operator = op;
    n->bin_expr_data.lhs = lhs;
    n->bin_expr_data.rhs = rhs;
    return n;
}

static struct ast_node *string_literal(struct ast_context *ctx, const char *string)
{
    struct ast_node* n = push_node(ctx, AST_LITERAL);
    n->literal_data.type = LITERAL_STRING;
    snprintf(n->literal_data.string, sizeof(n->literal_data.string), "%s", string);
    return n;
}

static struct ast_node *int_literal(struct ast_context *ctx, int value)
{
    struct ast_node* n = push_node(ctx, AST_LITERAL);
    n->literal_data.type = LITERAL_INTEGER;
    n->literal_data.integer = value;
    return n;
}

static struct ast_node *identifier(struct ast_context *ctx, const char *name)
{
    struct ast_node* n = push_node(ctx, AST_IDENTIFIER);
    snprintf(n->identifier_data.name, sizeof(n->identifier_data.name), "%s", name);
    return n;
}

static struct ast_node *expression(struct ast_context *ctx);

static struct ast_node *factor(struct ast_context *ctx)
{
    struct ast_node *n = NULL;

    if(!accept(ctx, TK_IDENT))
    {
        struct ast_node *ident = identifier(ctx, ctx->current_token->string);
        int operator = 0;
        struct ast_node *rhs = NULL;
        if(!accept(ctx, '='))
        {
        	operator = ctx->current_token->type;
            rhs = expression(ctx);
            if(!rhs)
            {
                printf("expected rhs... for assignment\n");
                return NULL;
            }
        } else
        {
            //TODO: function calls, other ident related stuff
            printf("got ident, unhandled...\n");
            return NULL;
        }
        return assignment_expr(ctx, operator, ident, rhs);
    } else if(!accept(ctx, '('))
    {
        n = expression(ctx);
        if(!n)
        {
            printf("no expression within parentheses\n");
            return NULL;
        }
        //expect end parenthese )
        if(accept(ctx, ')'))
        {
            printf("expected ending parenthese.. )\n");
            return NULL;
        }
        return n;
    } else if(!accept(ctx, '-') || !accept(ctx, '+') || !accept(ctx, '!') || !accept(ctx, '~'))
    {
        int operator = ctx->current_token->type;
        n = factor(ctx);
        if(!n)
        {
            printf("expected rhs... for unary expression %c\n", operator);
            return NULL;
        }
        return unary_expr(ctx, operator, true, n);
    } else if(!accept(ctx, TK_INTEGER))
    {
        n = int_literal(ctx, ctx->current_token->integer);
    } else
    {
		printf("expected integer.. got %d\n", ctx->current_token->type);
    }
    return n;
}

static struct ast_node *term(struct ast_context *ctx)
{
    struct ast_node *lhs = NULL;
	/*
    int negate = 0;
    if(accept(ctx, '-'))
        ++negate;
    */

    lhs = factor(ctx);
    if(!lhs)
    {
        printf("no term found\n");
        return NULL;
    }

    //first one is lhs
    //and it's now a binop
    
    while(!accept(ctx, '/') || !accept(ctx, '*') || !accept(ctx, '%'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = factor(ctx);
        if(!rhs)
        {
			printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}


static struct ast_node *add_and_subtract(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = term(ctx);
    if(!lhs)
        return NULL;
    while(!accept(ctx, '+') || !accept(ctx, '-'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = term(ctx);
        if(!rhs)
        {
			printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *bitwise_and(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = add_and_subtract(ctx);
    if(!lhs)
        return NULL;
    
    while(!accept(ctx, '&'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = add_and_subtract(ctx);
        if(!rhs)
        {
			printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *bitwise_xor(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = bitwise_and(ctx);
    if(!lhs)
        return NULL;
    
    while(!accept(ctx, '^'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = bitwise_and(ctx);
        if(!rhs)
        {
			printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *bitwise_or(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = bitwise_xor(ctx);
    if(!lhs)
        return NULL;
    
    while(!accept(ctx, '|'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = bitwise_xor(ctx);
        if(!rhs)
        {
			printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *expression(struct ast_context *ctx)
{
    return bitwise_or(ctx);
}

static void print_tabs(int n)
{
	for(int i = 0; i < n; ++i)
        putchar('\t');
}

static void print_ast(struct ast_node *n, int depth)
{
    print_tabs(depth);
    switch(n->type)
    {
    case AST_PROGRAM:
        printf("[program]\n");
        print_ast(n->program_data.entry, depth + 1);
        break;
    case AST_LITERAL:
        print_literal(&n->literal_data);
        break;
    case AST_BIN_EXPR:
        printf("binary expression operator %c\n", n->bin_expr_data.operator);
        printf("left");
        print_ast(n->bin_expr_data.lhs, depth + 1);
        printf("right");
        print_ast(n->bin_expr_data.rhs, depth + 1);
        break;
    case AST_UNARY_EXPR:
        printf("unary expression operator %c, (prefix: %s)\n", n->unary_expr_data.operator, n->unary_expr_data.prefix ? "yes" : "no");
        print_ast(n->unary_expr_data.argument, depth + 1);
        break;
    case AST_ASSIGNMENT_EXPR:
        printf("assignment expression\n");
        //TODO: fix
        break;
    default:
		printf("unhandled type %d\n", n->type);
        break;
    }
}

static int parse(struct ast_context *ctx)
{
    struct ast_node *n = NULL;
    
    n = expression(ctx);

    if(n)
    {
        if(ctx->verbose)
        	print_ast(n, 0);
    } else
        return ast_error(ctx, "expected integer");
    ctx->head = n;
    return 0;
}

//TODO: fix head/root expression/statement flow

int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool verbose)
{
    struct ast_context context = {
		.tokens = tokens,
        .num_tokens = num_tokens,
        .token_index = 0,
        .root = NULL,
        .head = NULL,
        .nodelist = NULL,
        .verbose = verbose
    };
    
	context.nodelist = linked_list_create( struct ast_node );

    struct ast_node t = {
            .parent = NULL,
            .type = AST_PROGRAM
    };
    context.root = linked_list_prepend(context.nodelist, t);

    context.head = context.root; //current head points to root

    if(!parse(&context))
    {
        *root = context.head;
        *ll = context.nodelist;
        return 0;
    }
    linked_list_destroy(&context.nodelist);
	return 1;
}
