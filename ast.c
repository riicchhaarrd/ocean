#include "ast.h"
#include "token.h"
#include "rhd/linked_list.h"

struct ast_context
{
    struct token *tokens;
    int num_tokens;
    int token_index;
    
    struct linked_list *node_list;
    struct ast_node *root_node;

    struct token *current_token;

    bool verbose;
};

static int accept(struct ast_context *ctx, int type)
{
    struct token *tk = &ctx->tokens[ctx->token_index];
    ctx->current_token = tk;
    if(tk->type != type)
    {
        //printf("tk->type %s (%d) != type %s (%d)\n", token_type_to_string(tk->type), tk->type, token_type_to_string(type), type);
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
	struct ast_node t = {
            .parent = NULL,
            .type = type
    };
    struct ast_node *node = linked_list_prepend(ctx->node_list, t);
    return node;
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
        	return assignment_expr(ctx, operator, ident, rhs);
		} else if(!accept(ctx, '('))
		{
			//function call
            n = push_node(ctx, AST_FUNCTION_CALL_EXPR);
            n->call_expr_data.callee = ident; //in this case the callee is just a identifier e.g a() where as a is the callee
            n->call_expr_data.numargs = 0;
            
			do
			{
                //if the function call is just without arguments, break instantly
				if(!accept(ctx, ')'))
                    break;
				struct ast_node *arg = expression(ctx);
                if(!arg)
				{
                    //arg got null and we got an error somewhere
                    //the allocated node n will be cleaned up by the zone allocated memory or in the list it's in later
                    printf("error in function call...\n");
                    return NULL;
				}
				//printf("arg %02X\n", arg);
                n->call_expr_data.arguments[n->call_expr_data.numargs++] = arg;
                
				if(!accept(ctx, ')')) //check whether we've hit the last parameter
                    break;
			} while(!accept(ctx, ','));
            return n;
        } else
        {
            //probably just using it as an identifier e.g func(arg)
            return ident;
            
            //TODO: function calls, other ident related stuff
            //printf("got ident, unhandled...\n");
            //return NULL;
        }
        return NULL; //can we be sure that n wasn't changed to be not NULL? just return NULL anyways    
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
    } else if(!accept(ctx, TK_STRING))
    {
        n = string_literal(ctx, ctx->current_token->string);
    } else if(!accept(ctx, TK_EOF))
    {
        n = push_node(ctx, AST_EXIT);
    } else
    {
		printf("expected integer.. got %s (%d)\n", token_type_to_string(ctx->current_token->type), ctx->current_token->type);
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
	//printf("(node type %s)\n", ast_node_type_strings[n->type]);
    
    print_tabs(depth);
    switch(n->type)
    {
    case AST_PROGRAM:
        printf("[program]\n");
        //print_ast(n->program_data.entry, depth + 1);
        printf("------------\n");
        linked_list_reversed_foreach(n->program_data.body, struct ast_node**, it,
        {
            print_ast(*it, depth + 1);
        });
        printf("------------\n");
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
        printf("assignment expression operator %c\n", n->assignment_expr_data.operator);
        printf("lhs");
        print_ast(n->assignment_expr_data.lhs, depth + 1);
        printf("rhs");
        print_ast(n->assignment_expr_data.rhs, depth + 1);
        break;
    case AST_IDENTIFIER:
        printf("identifier '%s'\n", n->identifier_data.name);
        break;
    case AST_EXIT:
        printf("exit\n");
        break;

    case AST_FUNCTION_CALL_EXPR:
    {
        struct ast_node **args = n->call_expr_data.arguments;
        int numargs = n->call_expr_data.numargs;
        struct ast_node *callee = n->call_expr_data.callee;
        assert(callee->type == AST_IDENTIFIER);
        //TODO: handle other callee types
        printf("function call expression '%s'\n", callee->identifier_data.name);
        for(int i = 0; i < numargs; ++i)
		{
            printf("\targ %d: %s\n", i, AST_NODE_TYPE_to_string(args[i]->type));
		}
	} break;
    
    default:
		printf("unhandled type %d\n", n->type);
        break;
    }
}

static int parse(struct ast_context *ctx)
{
    struct ast_node *n = NULL;

    do
    {
        n = expression(ctx);

        if(n)
        {
            //add it to the program
    		linked_list_prepend(ctx->root_node->program_data.body, n);

            if(n->type == AST_EXIT)
                break;
        }
    } while(n != NULL);
    //TODO: check for errors
    //return ast_error(ctx, "expected integer");
    return 0;
}

//TODO: fix head/root expression/statement flow

int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool verbose)
{
    struct ast_context context = {
		.tokens = tokens,
        .num_tokens = num_tokens,
        .token_index = 0,
        .root_node = NULL,
        .node_list = NULL,
        .verbose = verbose
    };
    
	context.node_list = linked_list_create( struct ast_node );

    struct ast_node t = {
            .parent = NULL,
            .type = AST_PROGRAM
    };
    //t.program_data.body = linked_list_create( void* );
    context.root_node = linked_list_prepend(context.node_list, t);
    context.root_node->program_data.body = linked_list_create(void*);
    
    if(!parse(&context))
    {
        if(context.verbose)
            print_ast(context.root_node, 0);

        *root = context.root_node;
        *ll = context.node_list;
        return 0;
    }
    
    linked_list_destroy(&t.program_data.body);
    linked_list_destroy(&context.node_list);
	return 1;
}
