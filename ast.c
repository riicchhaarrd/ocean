#include "ast.h"
#include "token.h"
#include "rhd/linked_list.h"
#include "std.h"

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
    if(ctx->token_index >= ctx->num_tokens)
        return type == TK_EOF ? 0 : 1;
    struct token *tk = &ctx->tokens[ctx->token_index];
    ctx->current_token = tk;
    if(tk->type != type)
    {
        //debug_printf("tk->type %s (%d) != type %s (%d)\n", token_type_to_string(tk->type), tk->type, token_type_to_string(type), type);
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
        debug_printf("expected type %d, got %d\n", type, ctx->tokens[ctx->token_index]);
        return 1;
    }
    return 0;
}

static int ast_error(struct ast_context *ctx, const char *errmessage)
{
    debug_printf("error: %s\n", errmessage);
    return 1;
}

static struct ast_node *push_node(struct ast_context *ctx, int type)
{
	struct ast_node t = {
            .parent = NULL,
            .type = type,
            .rvalue = 0
    };
    struct ast_node *node = linked_list_prepend(ctx->node_list, t);
    return node;
}

static struct ast_node *rvalue_node(struct ast_node *n)
{
    n->rvalue = 1;
    return n;
}

static struct ast_node *unary_expr(struct ast_context *ctx, int op, bool prefix, struct ast_node *arg)
{
    struct ast_node *n = push_node(ctx, AST_UNARY_EXPR);
    n->unary_expr_data.operator = op;
    n->unary_expr_data.prefix = prefix;
    n->unary_expr_data.argument = arg;
    return n;
}

static struct ast_node *array_subscript_expr(struct ast_context *ctx, struct ast_node *lhs, struct ast_node *rhs)
{
    struct ast_node *n = push_node(ctx, AST_MEMBER_EXPR);
    n->member_expr_data.computed = 0;
    n->member_expr_data.object = lhs;
    n->member_expr_data.property = rhs;
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
    /*
    if(lhs->type != AST_IDENTIFIER)
	{
        debug_printf("expected lhs to be a identifier got %s\n", AST_NODE_TYPE_to_string(lhs->type));
		return NULL;
	}
    */
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

static int type_qualifiers(struct ast_context *ctx, int *qualifiers)
{
    *qualifiers = TQ_NONE;
    if(!accept(ctx, TK_CONST))
        *qualifiers |= TQ_CONST;
    else
        return 1;
    return 0;
}

static struct ast_node *type_declaration(struct ast_context *ctx)
{
    struct ast_node *data_type_node = NULL;

    int pre_qualifiers = TQ_NONE;

    //TODO: implement const/volatile etc and other qualifiers
    type_qualifiers(ctx, &pre_qualifiers);
    
	if ( !accept( ctx, TK_T_CHAR ) || !accept( ctx, TK_T_SHORT ) || !accept( ctx, TK_T_INT ) ||
		 !accept( ctx, TK_T_FLOAT ) || !accept( ctx, TK_T_DOUBLE ) || !accept( ctx, TK_T_NUMBER ) || !accept(ctx, TK_T_VOID))
	{
		int primitive_type = ctx->current_token->type - TK_T_CHAR;
        
        int post_qualifiers = TQ_NONE;
        type_qualifiers(ctx, &post_qualifiers);
        int is_pointer = !accept(ctx, '*');

		struct ast_node* primitive_type_node = push_node( ctx, AST_PRIMITIVE_DATA_TYPE );
		primitive_type_node->primitive_data_type_data.primitive_type = primitive_type;
		primitive_type_node->primitive_data_type_data.qualifiers = pre_qualifiers | post_qualifiers;
        data_type_node = primitive_type_node;

        if(is_pointer)
		{
			struct ast_node* pointer_type_node = push_node( ctx, AST_POINTER_DATA_TYPE );
			pointer_type_node->pointer_data_type_data.data_type = primitive_type_node;
            data_type_node = pointer_type_node;
		}
	}
    return data_type_node;
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
        if(!accept(ctx, '('))
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
                    debug_printf("error in function call...\n");
                    return NULL;
				}
				//debug_printf("arg %02X\n", arg);
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
            //debug_printf("got ident, unhandled...\n");
            //return NULL;
        }
        return NULL; //can we be sure that n wasn't changed to be not NULL? just return NULL anyways    
    } else if(!accept(ctx, '('))
    {
        n = expression(ctx);
        if(!n)
        {
            debug_printf("no expression within parentheses\n");
            return NULL;
        }
        //expect end parenthese )
        if(accept(ctx, ')'))
        {
            debug_printf("expected ending parenthese.. )\n");
            return NULL;
        }
        return n;
    } else if(!accept(ctx, '-') || !accept(ctx, '+') || !accept(ctx, '!') || !accept(ctx, '~'))
    {
        int operator = ctx->current_token->type;
        n = expression(ctx);
        if(!n)
        {
            debug_printf("expected rhs... for unary expression %c\n", operator);
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
		n = push_node( ctx, AST_EXIT );
	} else if(!accept(ctx, '*'))
	{
        n = push_node(ctx, AST_DEREFERENCE);
        n->dereference_data.value = factor(ctx);
        if(!n->dereference_data.value)
		{
			debug_printf( "invalid expression\n" );
			return NULL;
		}
        return n;
    } else if(!accept(ctx, TK_SIZEOF))
    {
        if(accept(ctx, '('))
            return NULL;

        struct ast_node *so_node = push_node(ctx, AST_SIZEOF);

        struct ast_node *subject = type_declaration(ctx);
        if(!subject)
		{
            subject = expression(ctx);
            if(!subject)
                return NULL;
			so_node->sizeof_data.subject = subject;
		}
		if(accept(ctx, ')'))
            return NULL;
        return so_node;
	} else if(!accept(ctx, '&'))
    {
        n = push_node(ctx, AST_ADDRESS_OF);
        n->address_of_data.value = expression(ctx);
        if(!n->address_of_data.value)
		{
			debug_printf( "invalid expression\n" );
			return NULL;
		}
	} else
    {
		debug_printf("expected integer.. got %s (%d)\n", token_type_to_string(ctx->current_token->type), ctx->current_token->type);
    }
    return n;
}

static struct ast_node *array_subscripting(struct ast_context *ctx)
{
    struct ast_node *lhs = factor(ctx);
    if(!lhs)
        return NULL;
    while(!accept(ctx, '['))
    {
    	struct ast_node *rhs = expression(ctx);
        if(!rhs)
        {
			debug_printf("error.... no rhs..\n");
            return NULL;
        }
        if(accept(ctx, ']'))
		{
            debug_printf("expected ending bracket for array subscript\n");
            return NULL;
		}
		lhs = array_subscript_expr(ctx, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *term(struct ast_context *ctx)
{
    struct ast_node *lhs = NULL;
	/*
    int negate = 0;
    if(accept(ctx, '-'))
        ++negate;
    */

    lhs = array_subscripting(ctx);
    if(!lhs)
    {
        debug_printf("no term found\n");
        return NULL;
    }

    //first one is lhs
    //and it's now a binop
    
    while(!accept(ctx, '/') || !accept(ctx, '*') || !accept(ctx, '%'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = array_subscripting(ctx);
        if(!rhs)
        {
			debug_printf("error.... no rhs..\n");
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
			debug_printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *bitwise_shift(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = add_and_subtract(ctx);
    if(!lhs)
        return NULL;
    while(!accept(ctx, TK_LSHIFT) || !accept(ctx, TK_RSHIFT))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = add_and_subtract(ctx);
        if(!rhs)
        {
			debug_printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *relational(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = bitwise_shift(ctx);
    if(!lhs)
        return NULL;
    while(!accept(ctx, '>') || !accept(ctx, '<') || !accept(ctx, TK_LEQUAL) || !accept(ctx, TK_GEQUAL) || !accept(ctx, TK_EQUAL) || !accept(ctx, TK_NOT_EQUAL))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = bitwise_shift(ctx);
        if(!rhs)
        {
			debug_printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *bitwise_and(struct ast_context *ctx)
{
    /* TODO: add more expressions */
    struct ast_node *lhs = relational(ctx);
    if(!lhs)
        return NULL;
    
    while(!accept(ctx, '&'))
    {
        int operator = ctx->current_token->type;
    	struct ast_node *rhs = relational(ctx);
        if(!rhs)
        {
			debug_printf("error.... no rhs..\n");
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
			debug_printf("error.... no rhs..\n");
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
			debug_printf("error.... no rhs..\n");
            return NULL;
        }
        lhs = bin_expr(ctx, operator, lhs, rhs);
    }
    return lhs;
}

static struct ast_node *ternary(struct ast_context *ctx)
{
    struct ast_node *cond = bitwise_or(ctx);
    if(!cond)
        return NULL;
    
    if(!accept(ctx, '?'))
    {
    	struct ast_node *consequent = bitwise_or(ctx);
        if(!consequent)
        {
			debug_printf("error.... no consquent for ternary operator..\n");
            return NULL;
        }
        if(accept(ctx, ':'))
		{
            debug_printf("expected : for ternary operator\n");
			return NULL;
		}
    	struct ast_node *alternative = bitwise_or(ctx);
        if(!alternative)
        {
			debug_printf("error.... no alternative for ternary operator\n");
            return NULL;
        }
		struct ast_node* ternary_node = push_node( ctx, AST_TERNARY_EXPR );
        ternary_node->ternary_expr_data.condition = cond;
        ternary_node->ternary_expr_data.consequent = consequent;
        ternary_node->ternary_expr_data.alternative = alternative;
        return ternary_node;
	}
    return cond;
}

static struct ast_node *regular_assignment(struct ast_context *ctx)
{
    struct ast_node *lhs = ternary(ctx);
    if(!lhs) return NULL;

	static const int assignment_operators[] = {'=',TK_PLUS_ASSIGN,TK_MINUS_ASSIGN,TK_DIVIDE_ASSIGN,TK_MULTIPLY_ASSIGN,TK_MOD_ASSIGN,TK_AND_ASSIGN,TK_OR_ASSIGN,TK_XOR_ASSIGN};
    for(int i = 0; i < COUNT_OF(assignment_operators); ++i)
	{
        while ( !accept( ctx, assignment_operators[i] ) )
		{
            int operator = ctx->current_token->type;
			struct ast_node* rhs = ternary( ctx );
			if ( !rhs )
				return NULL;
			lhs = assignment_expr( ctx, operator, lhs, rhs );
		}
	}
    return lhs;
}

static struct ast_node *expression(struct ast_context *ctx)
{
    return regular_assignment(ctx);
}

static void print_tabs(int n)
{
	for(int i = 0; i < n; ++i)
        putchar('\t');
}

static void set_print_color(int n)
{
    printf("\x1B[%dm", 31 + (n % 7));
}

static void reset_print_color()
{
    printf("\x1B[0m");
}

static void print_ast(struct ast_node *n, int depth)
{
	//printf("(node type %s)\n", ast_node_type_strings[n->type]);

    int print_lines = 0;//depth > 1;
    set_print_color(depth);
    if(print_lines)
        printf("----------start of depth %d-------\n", depth);
    print_tabs(depth);
    switch(n->type)
    {
    case AST_BLOCK_STMT:
        printf("block statement\n");
        //printf("{\n");
		linked_list_reversed_foreach( n->block_stmt_data.body, struct ast_node**, it, { print_ast( *it, depth + 1 ); } );
		//printf( "}\n" );
		break;
    case AST_LITERAL:
        print_literal(&n->literal_data);
        break;
    case AST_BIN_EXPR:
        printf("binary expression operator %c\n", n->bin_expr_data.operator);
        //printf("left");
        print_ast(n->bin_expr_data.lhs, depth + 1);
        //printf("%c", n->bin_expr_data.operator);
        //printf("right");
        print_ast(n->bin_expr_data.rhs, depth + 1);
        break;
    case AST_UNARY_EXPR:
        printf("unary expression operator %c, (prefix: %s)\n", n->unary_expr_data.operator, n->unary_expr_data.prefix ? "yes" : "no");
        print_ast(n->unary_expr_data.argument, depth + 1);
        break;
    case AST_ASSIGNMENT_EXPR:
        printf("assignment expression operator %c\n", n->assignment_expr_data.operator);
        //printf("lhs");
        print_ast(n->assignment_expr_data.lhs, depth + 1);
        //printf("rhs");
        print_ast(n->assignment_expr_data.rhs, depth + 1);
        break;
    case AST_IDENTIFIER:
        printf("identifier '%s'\n", n->identifier_data.name);
        break;
    case AST_EXIT:
        printf("exit\n");
        break;

    case AST_PROGRAM:
		linked_list_reversed_foreach( n->program_data.body, struct ast_node**, it, { print_ast( *it, depth + 1 ); } );
        break;
    case AST_FUNCTION_DECL:
	{
        printf("function '%s'\n", n->func_decl_data.id->identifier_data.name);
        for(int i = 0; i < n->func_decl_data.numparms; ++i)
		{
            print_tabs(depth);
            printf("parm %d -> %s\n", i, n->func_decl_data.parameters[i]->variable_decl_data.id);
            print_ast(n->func_decl_data.parameters[i]->variable_decl_data.data_type, depth + 1);
		}

		print_ast(n->func_decl_data.body, depth + 1);
	} break;

    case AST_POINTER_DATA_TYPE:
	{
        printf("pointer data type '%s'\n", data_type_strings[n->pointer_data_type_data.data_type->primitive_data_type_data.primitive_type]);
	} break;

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

    case AST_IF_STMT:
    {
        struct ast_node *test = n->if_stmt_data.test;
        struct ast_node *consequent = n->if_stmt_data.consequent;
        printf("if statement\n");
        print_ast(test, depth + 1);
        print_ast(consequent, depth + 1);
    } break;

    case AST_MEMBER_EXPR:
    {
        printf("member expression\n");
        printf("object:\n");
        print_ast(n->member_expr_data.object, depth + 1);
        printf("property:\n");
        print_ast(n->member_expr_data.property, depth + 1);
    } break;

    case AST_VARIABLE_DECL:
	{
        printf("variable declaration\n");
        //printf("data_type = '%s', size = %d\n", data_type_strings[n->variable_decl_data.data_type], n->variable_decl_data.size);
        
        print_tabs(depth);
        printf("id:\n");
        print_ast(n->variable_decl_data.id, depth + 1);
        
        print_tabs(depth);
        printf("data type:\n");
        print_ast(n->variable_decl_data.data_type, depth + 1);
	} break;

    case AST_ARRAY_DATA_TYPE:
	{
        printf("array data type [%d]\n", n->array_data_type_data.array_size);
        print_ast(n->array_data_type_data.data_type, depth + 1);
	} break;

    case AST_PRIMITIVE_DATA_TYPE:
	{
        printf("type %s\n", data_type_strings[n->primitive_data_type_data.primitive_type]);
	} break;

	case AST_DEREFERENCE:
	{
        printf("dereferencing:\n");
        print_ast(n->dereference_data.value, depth + 1);
	} break;
    
    case AST_RETURN_STMT:
	{
        printf("return\n");
        print_ast(n->return_stmt_data.argument, depth + 1);
	} break;
    
    case AST_FOR_STMT:
    {
        if(n->for_stmt_data.init)
		{
			printf( "init:\n" );
			print_ast( n->for_stmt_data.init, depth + 1 );
		}
        if(n->for_stmt_data.test)
		{
			printf( "test:\n" );
			print_ast( n->for_stmt_data.test, depth + 1 );
		}
        if(n->for_stmt_data.update)
		{
			printf( "update:\n" );
			print_ast( n->for_stmt_data.update, depth + 1 );
		}
        if(n->for_stmt_data.body)
		{
			printf( "body:\n" );
			print_ast( n->for_stmt_data.body, depth + 1 );
		}
	} break;

	default:
		printf("unhandled type %s | %s:%d\n", AST_NODE_TYPE_to_string(n->type), __FILE__, __LINE__);
        break;
    }
    set_print_color(depth);
    if(print_lines)
        printf("-------end of depth %d--------------\n", depth);
    reset_print_color();
}

static int variable_declaration( struct ast_context* ctx, struct ast_node **out_decl_node )
{
    struct ast_node *type_decl = type_declaration(ctx);
    if(type_decl)
    {
        if ( accept( ctx, TK_IDENT ) )
		{
			debug_printf( "expected identifier for type declaration\n" );
			return 1;
		}
		struct ast_node* id = identifier( ctx, ctx->current_token->string );

		struct ast_node* decl_node = push_node( ctx, AST_VARIABLE_DECL );
		decl_node->variable_decl_data.id = id;
		decl_node->variable_decl_data.data_type = type_decl;

		if ( !accept( ctx, '[' ) )
		{
			struct ast_node* array_type_node = push_node( ctx, AST_ARRAY_DATA_TYPE );
			decl_node->variable_decl_data.data_type =
				array_type_node; // set our first array type on the declaration node it's data type
			while ( 1 )
			{
				if ( accept( ctx, TK_INTEGER ) )
				{
					debug_printf( "expected constant int array size\n" );
					return 1;
				}
				int dc = ctx->current_token->integer;
				if ( dc == 0 )
				{
					debug_printf( "array size can't be zero\n" );
					return 1;
				}
				if ( accept( ctx, ']' ) )
				{
					debug_printf( "expected ] after array type declaration\n" );
					return 1;
				}
				array_type_node->array_data_type_data.array_size = dc;
				array_type_node->array_data_type_data.data_type = type_decl;

				if ( accept( ctx, '[' ) )
					break;
				struct ast_node* new_node = push_node( ctx, AST_ARRAY_DATA_TYPE );
				array_type_node->array_data_type_data.data_type = new_node;
				array_type_node = new_node;
			}
		}
        *out_decl_node = decl_node;
        return 0;
	}
    *out_decl_node = NULL;
    return 0;
}

static struct ast_node *block(struct ast_context *ctx);
static struct ast_node *statement(struct ast_context *ctx)
{
    struct ast_node *decl_node = NULL;
    int ret = variable_declaration(ctx, &decl_node);
    if(ret)
        return NULL;
    
    if(decl_node != NULL)
    {
		if(accept(ctx, ';'))
		{
            debug_printf("expected ; after variable type declaration\n");
            return NULL;
		}
        return decl_node;
    }

    if(!accept(ctx, TK_EMIT))
	{
        if(accept(ctx, TK_INTEGER))
            return NULL;
        int opcode = ctx->current_token->integer;

		struct ast_node* n = push_node( ctx, AST_EMIT );
        n->emit_data.opcode = opcode;
        return n;
	} else if(!accept(ctx, TK_IF))
	{
        if(accept(ctx, '('))
		{
            debug_printf("expected ( after if\n");
			return NULL;
		}
    	struct ast_node *test = expression(ctx);
        if(!test)
		{
            debug_printf("invalid expression in if statement block\n");
            return NULL;
		}
		if(accept(ctx, ')'))
		{
            debug_printf("expected ) after if\n");
			return NULL;
		}
        struct ast_node *if_node = push_node(ctx, AST_IF_STMT);
        if_node->if_stmt_data.test = test;
        if(accept(ctx, '{'))
		{
    		struct ast_node *single_stmt = statement(ctx);
            if(!single_stmt)
                return NULL;
            if_node->if_stmt_data.consequent = single_stmt;
            return if_node;
		}
        struct ast_node *block_node = block(ctx);
        if(!block_node)
            return NULL;
        if_node->if_stmt_data.consequent = block_node;
        return if_node;
	} else if(!accept(ctx, TK_WHILE))
	{
        if(accept(ctx, '('))
		{
            debug_printf("expected ( after while\n");
			return NULL;
		}

        struct ast_node *test = expression(ctx);
        if(!test)
		{
            debug_printf("expected expression for while\n");
            return NULL;
		}

        if(accept(ctx, ')'))
		{
            debug_printf("expected ) after while\n");
			return NULL;
		}
		// TODO: FIXME make it clear whether we expect { here or in the block function
		struct ast_node* block_node = block( ctx );
		if ( !block_node )
			return NULL;
		struct ast_node* while_node = push_node( ctx, AST_WHILE_STMT );
        while_node->while_stmt_data.body = block_node;
        while_node->while_stmt_data.test = test;
        return while_node;
	} else if(!accept(ctx, TK_FOR))
	{
        if(accept(ctx, '('))
		{
            debug_printf("expected ( after for\n");
			return NULL;
		}

        struct ast_node *init, *test, *update;
        init = test = update = NULL;
        
        if(accept(ctx, ';'))
        {
			init = expression( ctx );
			if ( !init )
			{
				debug_printf( "invalid init expression in for statement block\n" );
				return NULL;
			}
            
			if ( accept( ctx, ';' ) )
			{
				debug_printf( "expected ; after expression\n" );
				return NULL;
			}
		}
        if(accept(ctx, ';'))
        {
			test = expression( ctx );
			if ( !test )
			{
				debug_printf( "invalid test expression in for statement block\n" );
				return NULL;
			}

			if ( accept( ctx, ';' ) )
			{
				debug_printf( "expected ; after expression\n" );
				return NULL;
			}
		}
        if(accept(ctx, ')'))
        {
			update = expression( ctx );
			if ( !update )
			{
				debug_printf( "invalid update expression in for statement block\n" );
				return NULL;
			}

			if ( accept( ctx, ')' ) )
			{
				debug_printf( "expected ) after for\n" );
				return NULL;
			}
		}
		// TODO: FIXME make it clear whether we expect { here or in the block function
		struct ast_node* block_node = block( ctx );
		if ( !block_node )
			return NULL;
		struct ast_node* for_node = push_node( ctx, AST_FOR_STMT );
		for_node->for_stmt_data.init = init;
		for_node->for_stmt_data.test = test;
		for_node->for_stmt_data.update = update;
		for_node->for_stmt_data.body = block_node;
		return for_node;
	} else if(!accept(ctx, TK_RETURN))
	{
        struct ast_node *ret_stmt = push_node(ctx, AST_RETURN_STMT);
        ret_stmt->return_stmt_data.argument = expression(ctx);
        if(!ret_stmt->return_stmt_data.argument)
            return NULL;
        if(accept(ctx, ';'))
		{
            debug_printf("expected ; after return statement\n");
			return NULL;
		}
		return ret_stmt;
	}

	struct ast_node *n = expression(ctx);
    if(!n)
        return NULL;
    if(n->type == AST_EXIT)
        return n;
    if(accept(ctx, ';'))
	{
        debug_printf("expected ; after expression statement got '%s'\n", token_type_to_string(ctx->current_token->type));
        return NULL;
	}
    return n;
}

//TODO: free/cleanup linked lists statements in block ast nodes either through zone allocation or manually
static struct ast_node *block(struct ast_context *ctx)
{
    accept(ctx, '{');
    
    struct ast_node *block_node = push_node(ctx, AST_BLOCK_STMT);
    block_node->block_stmt_data.body = linked_list_create(void*);

    while(1)
	{
		if(!accept(ctx, '}'))
            break;
        struct ast_node *n = statement(ctx);
        if(!n) return NULL;
        linked_list_prepend(block_node->block_stmt_data.body, n);

        if(n->type == AST_EXIT)
            break;
	}
    return block_node;
}

static struct ast_node *program(struct ast_context *ctx)
{
    struct ast_node *program_node = push_node(ctx, AST_PROGRAM);
    program_node->program_data.body = linked_list_create(void*);
    
    while(1)
    {
        if(!accept(ctx, TK_FUNCTION))
        {
            struct ast_node *decl = push_node(ctx, AST_FUNCTION_DECL);
            decl->func_decl_data.numparms = 0;
            if(accept(ctx, TK_IDENT))
            {
                debug_printf("expected ident after function keyword\n");
                return NULL;
            }
            struct ast_node *id = identifier(ctx, ctx->current_token->string);
            decl->func_decl_data.id = id;
            if(accept(ctx, '('))
            {
                debug_printf("expected ( after function\n");
                return NULL;
            }

            struct ast_node *parm_decl = NULL;
            while(1)
			{
				int ret = variable_declaration( ctx, &parm_decl );
                if(ret)
                    return NULL;

                if(parm_decl == NULL)
                    break;
                
                assert(parm_decl->variable_decl_data.id->type == AST_IDENTIFIER);
                //debug_printf("func parm %s\n", parm_decl->variable_decl_data.id->identifier_data.name);
                
                decl->func_decl_data.parameters[decl->func_decl_data.numparms++] = parm_decl;
                
                if(accept(ctx, ','))
                    break;
			}
            
            if(accept(ctx, ')'))
            {
                debug_printf("expected ( after function\n");
                return NULL;
            }

            struct ast_node *block_node = block(ctx);
            if(!block_node)
                return NULL;
            decl->func_decl_data.body = block_node;
            linked_list_prepend(program_node->program_data.body, decl);
        } else break;
    }
    return program_node;
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
    context.root_node = program(&context);
    
    if(context.root_node)
    {
        if(context.verbose)
            print_ast(context.root_node, 0);

        *root = context.root_node;
        *ll = context.node_list;
        return 0;
    }
    
    linked_list_destroy(&context.node_list);
	return 1;
}
