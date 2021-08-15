#include "token.h"
#include "ast.h"
#include "parse.h"
#include "rhd/linked_list.h"
#include "std.h"

struct ast_context
{
    struct parse_context parse_context;
    
    struct linked_list *node_list;
    struct ast_node *root_node;
    struct ast_node *function;

    struct ast_node *last_node;

    int verbose;
    jmp_buf jmp;
};

static void statement(struct ast_context *ctx, struct ast_node **node);

static int ast_accept(struct ast_context *ctx, int type)
{
    return parse_accept(&ctx->parse_context, type);
}

static struct token *ast_token(struct ast_context *ctx)
{
    return ctx->parse_context.current_token;
}

static void ast_assert_r(struct ast_context *ctx, int expr, const char *expr_str, const char *fmt, ...)
{
    if(expr)
        return;

	char buffer[512] = { 0 };
	va_list va;
	va_start( va, fmt );
	vsnprintf( buffer, sizeof( buffer ), fmt, va );
	debug_printf( "ast assert failed: '%s' %s\n", expr_str, buffer );
	va_end( va );

	longjmp(ctx->jmp, 1);
}

#define ast_assert(ctx, expr, ...) \
    ast_assert_r(ctx, (intptr_t)expr, #expr, ## __VA_ARGS__)

static void ast_error(struct ast_context *ctx, const char *fmt, ...)
{
	char buffer[512] = { 0 };
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    debug_printf("ast error: %s\n", buffer);
    va_end(va);
    
    longjmp(ctx->jmp, 1);
}

static void ast_expect_r(struct ast_context *ctx, int type, int lineno, const char *fmt, ...)
{
    struct token *tk = ctx->parse_context.current_token;
    if(!ast_accept(ctx, type))
        return;
    
	char buffer[512] = { 0 };
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    //TODO: print last 5-10 nodes that were pushed for more debug info
    debug_printf("[%d] syntax error: expected token '%s' got '%s' at line %d, last_node = '%s'\n%s\n", lineno, token_type_to_string(type), tk ? token_type_to_string(tk->type) : "null", tk->lineno, AST_NODE_TYPE_to_string(ctx->last_node->type), buffer);
    va_end(va);
    
    longjmp(ctx->jmp, 1);
}

#define ast_expect(ctx, type, ...) \
    ast_expect_r(ctx, type, __LINE__, ## __VA_ARGS__)

static struct ast_node *push_node(struct ast_context *ctx, int type)
{
	struct ast_node t = {
            .parent = NULL,
            .type = type,
            .rvalue = 0
    };
    struct ast_node *node = linked_list_prepend(ctx->node_list, t);
    ctx->last_node = node;
    return node;
}

static struct ast_node *rvalue_node(struct ast_node *n)
{
    n->rvalue = 1;
    return n;
}

static struct ast_node *unary_expr(struct ast_context *ctx, int op, int prefix, struct ast_node *arg)
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

void expression_sequence(struct ast_context *ctx, struct ast_node **node);

static int type_qualifiers(struct ast_context *ctx, int *qualifiers)
{
    *qualifiers = TQ_NONE;
    if(!ast_accept(ctx, TK_CONST))
        *qualifiers |= TQ_CONST;
    else
        return 1;
    return 0;
}

static int type_declaration(struct ast_context *ctx, struct ast_node **data_type_node)
{
    *data_type_node = NULL;

    int pre_qualifiers = TQ_NONE;

    //TODO: implement const/volatile etc and other qualifiers
    type_qualifiers(ctx, &pre_qualifiers);
    
	if ( !ast_accept( ctx, TK_T_CHAR ) || !ast_accept( ctx, TK_T_SHORT ) || !ast_accept( ctx, TK_T_INT ) ||
		 !ast_accept( ctx, TK_T_FLOAT ) || !ast_accept( ctx, TK_T_DOUBLE ) || !ast_accept(ctx, TK_T_VOID))
	{
		int primitive_type = ast_token(ctx)->type - TK_T_CHAR;
        
        int post_qualifiers = TQ_NONE;
        type_qualifiers(ctx, &post_qualifiers);
        int np = 0;
        while(!ast_accept(ctx, '*'))
            ++np;

		struct ast_node* primitive_type_node = push_node( ctx, AST_PRIMITIVE_DATA_TYPE );
		primitive_type_node->primitive_data_type_data.primitive_type = primitive_type;
		primitive_type_node->primitive_data_type_data.qualifiers = pre_qualifiers | post_qualifiers;
        *data_type_node = primitive_type_node;

        for(int i = 0; i < np; ++i)
		{
			struct ast_node* pointer_type_node = push_node( ctx, AST_POINTER_DATA_TYPE );
			pointer_type_node->pointer_data_type_data.data_type = *data_type_node;
            *data_type_node = pointer_type_node;
		}
	}
    return *data_type_node ? 0 : ( pre_qualifiers == TQ_NONE ? 0 : 1 );
}

static void expression(struct ast_context *ctx, struct ast_node **node);
static void factor( struct ast_context* ctx, struct ast_node **node );

static struct ast_node *ident_factor(struct ast_context *ctx)
{
	struct ast_node* ident = identifier( ctx, ast_token(ctx)->string );
	if ( !ast_accept( ctx, '(' ) )
	{
		struct ast_node* n = push_node( ctx, AST_FUNCTION_CALL_EXPR );
		n->call_expr_data.callee = ident;
		n->call_expr_data.numargs = 0;
		do
		{
			if ( !ast_accept( ctx, ')' ) )
				break;
			struct ast_node* arg;
			expression( ctx, &arg );
			n->call_expr_data.arguments[n->call_expr_data.numargs++] = arg;
			if ( !ast_accept( ctx, ')' ) )
				break;
		} while ( !ast_accept( ctx, ',' ) );
		return n;
	}
	return ident;
}

static struct ast_node *parens_factor(struct ast_context *ctx)
{
	struct ast_node* n;
    expression( ctx, &n );
	ast_expect( ctx, ')', "no ending )" );
	return n;
}

static struct ast_node *unary_expr_factor(struct ast_context *ctx)
{
	int operator = ast_token(ctx)->type;
	struct ast_node* n;
    expression( ctx, &n );
	return unary_expr( ctx, operator, 1, n );
}

static struct ast_node *integer_factor(struct ast_context *ctx)
{
	return int_literal( ctx, ast_token(ctx)->integer );
}

static struct ast_node *string_factor(struct ast_context *ctx)
{
	return string_literal( ctx, ast_token(ctx)->string );
}

static struct ast_node *dereference_factor(struct ast_context *ctx)
{
	struct ast_node* n = push_node( ctx, AST_DEREFERENCE );
	factor( ctx, &n->dereference_data.value );
	return n;
}

static struct ast_node *sizeof_factor(struct ast_context *ctx)
{
	ast_expect( ctx, '(', "sizeof expecting (" );
	struct ast_node* so_node = push_node( ctx, AST_SIZEOF );
	int td = type_declaration( ctx, &so_node->sizeof_data.subject );
	ast_assert( ctx, !td, "error in type declaration" );
	if ( !so_node->sizeof_data.subject )
		expression( ctx, &so_node->sizeof_data.subject );
	ast_expect( ctx, ')', "sizeof expecting )" );
	return so_node;
}

static struct ast_node *address_of_factor(struct ast_context *ctx)
{
	struct ast_node* n = push_node( ctx, AST_ADDRESS_OF );
    //TODO: FIXME proper precedence, see order of evaluation
    //https://en.cppreference.com/w/c/language/operator_precedence
	void array_subscripting( struct ast_context * ctx, struct ast_node * *node );
	array_subscripting( ctx, &n->address_of_data.value );
	return n;
}

struct ast_node_type_function
{
    int type;
    struct ast_node *(*function)();
};

static struct ast_node_type_function factors[] = {
    { TK_IDENT, ident_factor },
    { '(', parens_factor },
    { '-', unary_expr_factor },
    { '+', unary_expr_factor },
    { '!', unary_expr_factor },
    { '~', unary_expr_factor },
    { TK_PLUS_PLUS, unary_expr_factor },
    { TK_MINUS_MINUS, unary_expr_factor },
    { TK_INTEGER, integer_factor },
    { TK_STRING, string_factor },
    { '*', dereference_factor },
    { TK_SIZEOF, sizeof_factor },
    { '&', address_of_factor }
};

static void factor( struct ast_context* ctx, struct ast_node **node )
{
    *node = NULL;
    for(int i = 0; i < COUNT_OF(factors); ++i)
	{
        if(!ast_accept(ctx, factors[i].type))
		{
			*node = factors[i].function( ctx );
			return;
		}
	}
	ast_error(ctx, "expected factor");
}

static void postfix(struct ast_context *ctx, struct ast_node **node)
{
    factor(ctx,node);
	while ( !ast_accept( ctx, TK_PLUS_PLUS ) || !ast_accept( ctx, TK_MINUS_MINUS ) )
		*node = unary_expr( ctx, ast_token( ctx )->type, 0, *node );
}

void array_subscripting(struct ast_context *ctx, struct ast_node **node)
{
    postfix(ctx, node);
    while(!ast_accept(ctx, '['))
    {
        struct ast_node *rhs;
        expression(ctx, &rhs);
        ast_expect(ctx, ']', "no ending bracket for array subscript");
		*node = array_subscript_expr(ctx, *node, rhs);
    }
}

static void term(struct ast_context *ctx, struct ast_node **node)
{
    array_subscripting(ctx, node);
    
    while(!ast_accept(ctx, '/') || !ast_accept(ctx, '*') || !ast_accept(ctx, '%'))
    {
		int operator = ast_token(ctx)->type;
		struct ast_node* rhs;
		array_subscripting( ctx , &rhs );
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}


static void add_and_subtract(struct ast_context *ctx, struct ast_node **node)
{
    term(ctx, node);
    while(!ast_accept(ctx, '+') || !ast_accept(ctx, '-'))
    {
        int operator = ast_token(ctx)->type;
    	struct ast_node *rhs;
        term(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_shift(struct ast_context *ctx, struct ast_node **node)
{
    add_and_subtract(ctx, node);
    while(!ast_accept(ctx, TK_LSHIFT) || !ast_accept(ctx, TK_RSHIFT))
    {
        int operator = ast_token(ctx)->type;
    	struct ast_node *rhs;
        add_and_subtract(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void relational(struct ast_context *ctx, struct ast_node **node)
{
    bitwise_shift(ctx, node);
    while(!ast_accept(ctx, '>') || !ast_accept(ctx, '<') || !ast_accept(ctx, TK_LEQUAL) || !ast_accept(ctx, TK_GEQUAL) || !ast_accept(ctx, TK_EQUAL) || !ast_accept(ctx, TK_NOT_EQUAL))
    {
        int operator = ast_token(ctx)->type;
    	struct ast_node *rhs;
        bitwise_shift(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_and(struct ast_context *ctx, struct ast_node **node)
{
    relational(ctx, node);
    while(!ast_accept(ctx, '&'))
    {
        int operator = ast_token(ctx)->type;
    	struct ast_node *rhs;
        relational(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_xor(struct ast_context *ctx, struct ast_node **node)
{
    bitwise_and(ctx, node);
    while(!ast_accept(ctx, '^'))
    {
        int operator = ast_token(ctx)->type;
    	struct ast_node *rhs;
        bitwise_and(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_or(struct ast_context *ctx, struct ast_node **node)
{
    bitwise_xor(ctx, node);
    while(!ast_accept(ctx, '|'))
    {
        int operator = ast_token(ctx)->type;
    	struct ast_node *rhs;
        bitwise_xor(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void ternary(struct ast_context *ctx, struct ast_node **node)
{
    bitwise_or(ctx, node);
    
    while(!ast_accept(ctx, '?'))
    {
    	struct ast_node *consequent, *alternative, *ternary_node;
        bitwise_or(ctx, &consequent);
        ast_expect(ctx, ':', "expected : for ternary operator");
    	bitwise_or(ctx, &alternative);
		ternary_node = push_node( ctx, AST_TERNARY_EXPR );
        ternary_node->ternary_expr_data.condition = *node;
        ternary_node->ternary_expr_data.consequent = consequent;
        ternary_node->ternary_expr_data.alternative = alternative;
        *node = ternary_node;
	}
}

static void regular_assignment(struct ast_context *ctx, struct ast_node **node)
{
    ternary(ctx, node);

	static const int assignment_operators[] = {'=',TK_PLUS_ASSIGN,TK_MINUS_ASSIGN,TK_DIVIDE_ASSIGN,TK_MULTIPLY_ASSIGN,TK_MOD_ASSIGN,TK_AND_ASSIGN,TK_OR_ASSIGN,TK_XOR_ASSIGN};
    for(int i = 0; i < COUNT_OF(assignment_operators); ++i)
	{
        while ( !ast_accept( ctx, assignment_operators[i] ) )
		{
            int operator = ast_token(ctx)->type;
			struct ast_node* rhs;
            ternary( ctx , &rhs );
			*node = assignment_expr( ctx, operator, *node, rhs );
		}
	}
}

static void expression( struct ast_context* ctx, struct ast_node** node )
{
	regular_assignment( ctx, node );
}

void expression_sequence(struct ast_context *ctx, struct ast_node **node)
{
	struct ast_node* seq = push_node( ctx, AST_SEQ_EXPR );
	seq->seq_expr_data.numexpr = 0;
	struct ast_node* n;
	do
	{
		regular_assignment( ctx, &n );
		seq->seq_expr_data.expr[seq->seq_expr_data.numexpr++] = n;
	} while ( !ast_accept( ctx, ',' ) );
	*node = seq->seq_expr_data.numexpr == 1 ? n : seq;
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
	case AST_SEQ_EXPR:
	{
		for ( int i = 0; i < n->seq_expr_data.numexpr; ++i )
		{
			print_ast( n->seq_expr_data.expr[i], depth + 1 );
		}
	}
	break;
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
        printf("pointer data type\n");
        print_ast(n->pointer_data_type_data.data_type, depth + 1);
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
            print_ast(args[i], depth + 1);
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

	case AST_WHILE_STMT:
	{
		struct ast_node* test = n->while_stmt_data.test;
		printf( "while statement\n" );
		print_ast( test, depth + 1 );
		print_ast( n->while_stmt_data.body, depth + 1 );
	}
	break;
    
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

    case AST_SIZEOF:
	{
        printf("sizeof\n");
        print_ast(n->sizeof_data.subject, depth + 1);
	} break;

    case AST_ADDRESS_OF:
	{
        printf("address of\n");
        print_ast(n->address_of_data.value, depth + 1);
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

static void variable_declaration( struct ast_context* ctx, struct ast_node** out_decl_node, int is_param )
{
	struct ast_node* type_decl = NULL;
	int td = type_declaration(ctx, &type_decl);
    ast_assert(ctx, !td, "error in type declaration");
	if(type_decl)
    {
        ast_expect(ctx, TK_IDENT, "expected identifier for type declaration");
		struct ast_node* id = identifier( ctx, ast_token(ctx)->string );

		struct ast_node* decl_node = push_node( ctx, AST_VARIABLE_DECL );
		assert( ctx->function );
		if ( !is_param )
			ctx->function->func_decl_data.declarations[ctx->function->func_decl_data.numdeclarations++] = decl_node;
		decl_node->variable_decl_data.id = id;
		decl_node->variable_decl_data.data_type = type_decl;
        decl_node->variable_decl_data.initializer_value = NULL;

		if ( !ast_accept( ctx, '[' ) )
		{
			struct ast_node* array_type_node = push_node( ctx, AST_ARRAY_DATA_TYPE );
			decl_node->variable_decl_data.data_type =
				array_type_node; // set our first array type on the declaration node it's data type
			while ( 1 )
			{
                ast_expect(ctx, TK_INTEGER, "expected constant int array size");
				int dc = ast_token(ctx)->integer;
				ast_assert( ctx, dc > 0, "array size can't be zero" );
                ast_expect(ctx, ']', "expected ] after array type declaration");
				array_type_node->array_data_type_data.array_size = dc;
				array_type_node->array_data_type_data.data_type = type_decl;
				if ( ast_accept( ctx, '[' ) )
					break;
				struct ast_node* new_node = push_node( ctx, AST_ARRAY_DATA_TYPE );
				array_type_node->array_data_type_data.data_type = new_node;
				array_type_node = new_node;
			}
		}

        //initializer value
        if(!ast_accept(ctx, '='))
		{
            struct ast_node *initializer_value;
            expression(ctx, &initializer_value);
            ast_assert(ctx, initializer_value, "expected initializer value after =");
            decl_node->variable_decl_data.initializer_value = initializer_value;
		}

		*out_decl_node = decl_node;
        return;
	}
    *out_decl_node = NULL;
}

static struct ast_node *init_statement(struct ast_context *ctx)
{
	struct ast_node* decl_node;
	variable_declaration( ctx, &decl_node, 0 );
	if ( !decl_node )
		regular_assignment( ctx, &decl_node );
	if ( !decl_node )
		return NULL;
	// TODO: FIXME this is not correct in this case
	/*
	  for(int i = 0, j = 1; i < 10; ++i);
	  //gives error, unless you specify int j; before
	 */
	if ( !ast_accept( ctx, ',' ) )
	{
		struct ast_node* seq = push_node( ctx, AST_SEQ_EXPR );
		seq->seq_expr_data.numexpr = 0;
		seq->seq_expr_data.expr[seq->seq_expr_data.numexpr++] = decl_node;
		do
		{
            struct ast_node *n;
			expression( ctx, &n );
			seq->seq_expr_data.expr[seq->seq_expr_data.numexpr++] = n;
		} while(!ast_accept(ctx, ','));
        return seq;
	}
	return decl_node;
}

static struct ast_node *block_statement(struct ast_context *ctx)
{
	struct ast_node* n = push_node( ctx, AST_BLOCK_STMT );
	n->block_stmt_data.body = linked_list_create( void* );

	while ( 1 )
	{
		if ( !ast_accept( ctx, '}' ) )
			break;
		struct ast_node* stmt;
		statement( ctx, &stmt );
		ast_assert( ctx, stmt, "expected statement" );
		linked_list_prepend( n->block_stmt_data.body, stmt );
	}
	return n;
}

static struct ast_node* emit_statement( struct ast_context* ctx )
{
	ast_expect( ctx, TK_INTEGER, "expected integer after emit statement" );
	int opcode = ast_token(ctx)->integer;
	struct ast_node* n = push_node( ctx, AST_EMIT );
	n->emit_data.opcode = opcode;
	return n;
}

static struct ast_node* if_statement( struct ast_context* ctx )
{
	ast_expect( ctx, '(', "expected ( after if" );
	struct ast_node *test, *body, *if_node;
	if_node = push_node( ctx, AST_IF_STMT );
	expression( ctx, &if_node->if_stmt_data.test );
	ast_expect( ctx, ')', "expected ) after if" );
	statement( ctx, &if_node->if_stmt_data.consequent );
    if(!ast_accept(ctx, TK_ELSE))
	{
		statement( ctx, &if_node->if_stmt_data.alternative );
	}
	return if_node;
}

static struct ast_node* break_statement( struct ast_context* ctx )
{
	// TODO: maybe move this to iteration only statements function
	return push_node( ctx, AST_BREAK_STMT );
}

static struct ast_node* while_statement( struct ast_context* ctx )
{
	ast_expect( ctx, '(', "expected ( after while\n" );

	struct ast_node* test, *body, *while_node;
	expression( ctx, &test );
	ast_assert( ctx, test, "expected expression after while" );
	ast_expect( ctx, ')', "expected ) after while" );

    statement( ctx, &body );
	ast_assert( ctx, body, "while has no body" );
    
    while_node = push_node( ctx, AST_WHILE_STMT );
	while_node->while_stmt_data.body = body;
	while_node->while_stmt_data.test = test;
	return while_node;
}

static struct ast_node* do_statement( struct ast_context* ctx )
{
	struct ast_node* test, *body, *node;
    
    statement( ctx, &body );
	ast_assert( ctx, body, "do has no body" );
    ast_expect(ctx, TK_WHILE, "expected while after do statement");
    
	ast_expect( ctx, '(', "expected ( after while\n" );

	expression( ctx, &test );
	ast_assert( ctx, test, "expected expression after while" );
    
	ast_expect( ctx, ')', "expected ) after while" );
    
    node = push_node( ctx, AST_DO_WHILE_STMT );
    node->do_while_stmt_data.body = body;
    node->do_while_stmt_data.test = test;
    return node;
}

static struct ast_node* for_statement( struct ast_context* ctx )
{
	ast_expect( ctx, '(', "expected ( after for" );
	struct ast_node *init, *test, *update;

	if ( ast_accept( ctx, ';' ) )
	{
		init = init_statement( ctx );
		ast_assert( ctx, init, "invalid init statement in for statement block" );
		ast_expect( ctx, ';', "expected ; after init statement" );
	}
	if ( ast_accept( ctx, ';' ) )
	{
		expression( ctx, &test );
		ast_assert( ctx, test, "invalid test expression in for statement block" );
		ast_expect( ctx, ';', "expected ; after expression" );
	}
	if ( ast_accept( ctx, ')' ) )
	{
		expression_sequence( ctx, &update );
		ast_assert( ctx, update, "invalid update expression in for statement block" );
		ast_expect( ctx, ')', "expected ) after for" );
	}

	struct ast_node* body;
	statement( ctx, &body );
	ast_assert( ctx, body, "no body for for statement" );
	struct ast_node* for_node = push_node( ctx, AST_FOR_STMT );
	for_node->for_stmt_data.init = init;
	for_node->for_stmt_data.test = test;
	for_node->for_stmt_data.update = update;
	for_node->for_stmt_data.body = body;
	return for_node;
}

static struct ast_node* return_statement( struct ast_context* ctx )
{
	struct ast_node* ret_stmt = push_node( ctx, AST_RETURN_STMT );
	ret_stmt->return_stmt_data.argument = NULL;
	if ( !ast_accept( ctx, ';' ) )
		return ret_stmt;
	expression( ctx, &ret_stmt->return_stmt_data.argument );
	return ret_stmt;
}

static struct ast_node_type_function statements[] = {
    { TK_EMIT, emit_statement },
    { TK_IF, if_statement },
    { TK_BREAK, break_statement },
    { TK_WHILE, while_statement },
    { TK_DO, do_statement },
    { TK_FOR, for_statement },
    { TK_RETURN, return_statement },
    { '{', block_statement }
};

static void statement_node(struct ast_context *ctx, struct ast_node **node)
{
    *node = NULL;
    for(int i = 0; i < COUNT_OF(statements); ++i)
	{
        if(!ast_accept(ctx, statements[i].type))
		{
            *node = statements[i].function(ctx);
            return;
		}
	}
	*node = init_statement(ctx);
}

static void statement(struct ast_context *ctx, struct ast_node **node)
{
    if(!ast_accept(ctx, ';'))
	{
		*node = push_node( ctx, AST_EMPTY );
        return;
	}
	statement_node( ctx, node );
    int type = (*node)->type;
	if ( type == AST_BLOCK_STMT || type == AST_IF_STMT || type == AST_WHILE_STMT ||
		 type == AST_FOR_STMT )
	{
        return;
	}
	ast_expect(ctx, ';', "expected ; after statement");
}

static struct ast_node *program(struct ast_context *ctx)
{
    struct ast_node *program_node = push_node(ctx, AST_PROGRAM);
    program_node->program_data.body = linked_list_create(void*);
    
    while(1)
    {
        if(!ast_accept(ctx, TK_EOF)) break;
        
        struct ast_node* type_decl = NULL;
        int td = type_declaration( ctx , &type_decl );
        ast_assert(ctx, !td, "error in type declaration");
		// TODO: implement global variables assignment, function prototypes and a preprocessor
		if ( !type_decl )
			ast_error( ctx, "expected function return type got '%s'", token_type_to_string( parse_token(&ctx->parse_context)->type ) );
        
		struct ast_node* decl = push_node( ctx, AST_FUNCTION_DECL );
		decl->func_decl_data.return_data_type = type_decl;
		decl->func_decl_data.numparms = 0;
		decl->func_decl_data.variadic = 0;
		decl->func_decl_data.numdeclarations = 0;
		ctx->function = decl;

		ast_expect( ctx, TK_IDENT, "expected ident after function" );
		struct ast_node* id = identifier( ctx, ast_token(ctx)->string );
		decl->func_decl_data.id = id;
		ast_expect( ctx, '(', "expected ( after function" );

		struct ast_node* parm_decl = NULL;
		while ( 1 )
		{
			if ( !ast_accept( ctx, TK_DOT_THREE_TIMES ) )
			{
				decl->func_decl_data.variadic = 1;
				break;
			}
			variable_declaration( ctx, &parm_decl, 1 );

			if ( parm_decl == NULL )
				break;

			assert( parm_decl->variable_decl_data.id->type == AST_IDENTIFIER );
			// debug_printf("func parm %s\n", parm_decl->variable_decl_data.id->identifier_data.name);

			decl->func_decl_data.parameters[decl->func_decl_data.numparms++] = parm_decl;

			if ( ast_accept( ctx, ',' ) )
				break;
		}

		ast_expect( ctx, ')', "expected ) after function" );

		struct ast_node* block_node;
        statement_node(ctx, &block_node);
        ast_assert(ctx, block_node->type == AST_BLOCK_STMT, "expected { after function");
		decl->func_decl_data.body = block_node;
		linked_list_prepend( program_node->program_data.body, decl );
	}
    return program_node;
}

//TODO: fix head/root expression/statement flow
int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool verbose)
{
    struct ast_context context = {
        .root_node = NULL,
        .node_list = NULL,
        .verbose = verbose,
        .function = NULL,
        .last_node = NULL
    };

    context.parse_context.current_token = NULL;
    context.parse_context.num_tokens = num_tokens;
    context.parse_context.token_index = 0;
    context.parse_context.tokens = tokens;

    if(setjmp(context.jmp))
    {
        printf("generating AST failed\n");
		goto fail;
    }
    
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
fail:
    linked_list_destroy(&context.node_list);
	return 1;
}
