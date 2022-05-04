#include "token.h"
#include "ast.h"
#include "parse.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"
#include "std.h"

static ast_node_t *push_node(ast_context_t *ctx, int type)
{
	ast_node_t *n = arena_alloc(ctx->allocator, sizeof(ast_node_t));
	n->parent = NULL;
	n->type = type;
	n->rvalue = 0;
	return n;
}

static ast_node_t *identifier(ast_context_t *ctx, const char *name)
{
    ast_node_t* n = push_node(ctx, AST_IDENTIFIER);
    snprintf(n->identifier_data.name, sizeof(n->identifier_data.name), "%s", name);
    return n;
}

void ast_init_context(ast_context_t *ctx, arena_t *allocator)
{
	ctx->allocator = allocator;
	
    ast_node_t *n = push_node(ctx, AST_PROGRAM);
    n->program_data.body = linked_list_create_with_custom_allocator(void*, ctx->allocator, arena_alloc);
	ctx->program_node = n;
	
	ast_node_t* fn = push_node( ctx, AST_FUNCTION_DECL );
	fn->func_decl_data.return_data_type = NULL;
	fn->func_decl_data.numparms = 0;
	fn->func_decl_data.variadic = 0;
	fn->func_decl_data.numdeclarations = 0;
	fn->func_decl_data.id = identifier(ctx, "default_function");
	ctx->default_function = fn;
	
	ctx->verbose = 0;
	ctx->function = ctx->default_function;
	ctx->type_definitions = hash_map_create_with_custom_allocator(ast_node_t, ctx->allocator, arena_alloc);
	ctx->numtypes = 0;
}

static void statement(ast_context_t *ctx, ast_node_t **node);

static int ast_accept(ast_context_t *ctx, int type)
{
    return parse_accept(&ctx->parse_context, type);
}

static struct token *ast_token(ast_context_t *ctx)
{
    return ctx->parse_context.current_token;
}

static void ast_assert_r(ast_context_t *ctx, int expr, const char *expr_str, const char *fmt, ...)
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


#define ast_error(ctx, fmt, ...) \
	ast_error_r(ctx, __LINE__, __FILE__, fmt, ## __VA_ARGS__)

static void ast_error_r(ast_context_t *ctx, int lineno, const char *file, const char *fmt, ...)
{
	char buffer[512] = { 0 };
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
	const char *func_name = ctx->function ? ctx->function->func_decl_data.id->identifier_data.name : NULL;
    struct token *tk = parse_token(&ctx->parse_context);
    printf("AST Error: %s at line number %d in function '%s'.\n", buffer, tk->lineno, func_name);
    va_end(va);
    
    longjmp(ctx->jmp, 1);
}

static void ast_expect_r(ast_context_t *ctx, int type, int lineno, const char *fmt, ...)
{
	struct token* tk = ctx->parse_context.current_token;
	if(!ast_accept(ctx, type))
        return;
	const char *func_name = ctx->function ? ctx->function->func_decl_data.id->identifier_data.name : NULL;
    
	char buffer[512] = { 0 };
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    //TODO: print last 5-10 nodes that were pushed for more debug info
    debug_printf("Syntax Error: expected token '%s' got '%s' message: '%s' at line %d in function '%s'.\n", token_type_to_string(type), tk ? token_type_to_string(tk->type) : "null", buffer, tk->lineno, func_name);
    va_end(va);
    
    longjmp(ctx->jmp, 1);
}

#define ast_expect(ctx, type, ...) \
    ast_expect_r(ctx, type, __LINE__, ## __VA_ARGS__)

static ast_node_t *rvalue_node(ast_node_t *n)
{
    n->rvalue = 1;
    return n;
}

static ast_node_t *unary_expr(ast_context_t *ctx, int op, int prefix, ast_node_t *arg)
{
    ast_node_t *n = push_node(ctx, AST_UNARY_EXPR);
    n->unary_expr_data.operator = op;
    n->unary_expr_data.prefix = prefix;
    n->unary_expr_data.argument = arg;
    return n;
}

static ast_node_t *array_subscript_expr(ast_context_t *ctx, ast_node_t *lhs, ast_node_t *rhs)
{
    ast_node_t *n = push_node(ctx, AST_MEMBER_EXPR);
    n->member_expr_data.computed = 0;
    n->member_expr_data.object = lhs;
    n->member_expr_data.property = rhs;
    n->member_expr_data.as_pointer = 0;
    return n;
}

static ast_node_t *bin_expr(ast_context_t *ctx, int op, ast_node_t *lhs, ast_node_t *rhs)
{
    ast_node_t *n = push_node(ctx, AST_BIN_EXPR);
    n->bin_expr_data.operator = op;
    n->bin_expr_data.lhs = lhs;
    n->bin_expr_data.rhs = rhs;
    return n;
}

static ast_node_t *assignment_expr(ast_context_t *ctx, int op, ast_node_t *lhs, ast_node_t *rhs)
{
	/*
	if(lhs->type != AST_IDENTIFIER)
	{
		debug_printf("expected lhs to be a identifier got %s\n", ast_node_type_t_to_string(lhs->type));
		return NULL;
	}
	*/
	ast_node_t *n = push_node(ctx, AST_ASSIGNMENT_EXPR);
    n->bin_expr_data.operator = op;
    n->bin_expr_data.lhs = lhs;
    n->bin_expr_data.rhs = rhs;
    return n;
}

static ast_node_t *string_literal(ast_context_t *ctx, const char *string)
{
    ast_node_t* n = push_node(ctx, AST_LITERAL);
    n->literal_data.type = LITERAL_STRING;
    snprintf(n->literal_data.string, sizeof(n->literal_data.string), "%s", string);
    return n;
}

static ast_node_t *int_literal(ast_context_t *ctx, integer_t value)
{
    ast_node_t* n = push_node(ctx, AST_LITERAL);
    n->literal_data.type = LITERAL_INTEGER;
    n->literal_data.integer = value;
    return n;
}

static void add_type_definition(ast_context_t *ctx, const char *key, ast_node_t *n)
{
	hash_map_insert(ctx->type_definitions, key, *n);
	++ctx->numtypes;
}

static ast_node_t *find_type_definition(ast_context_t *ctx, const char *key)
{
    ast_node_t *n = hash_map_find(ctx->type_definitions, key);
	if(!n)
		return NULL;
	if(n->type == AST_TYPEDEF)
		return n->typedef_data.type;
	return n;
}

void expression_sequence(ast_context_t *ctx, ast_node_t **node);

static int type_qualifiers(ast_context_t *ctx, int *qualifiers)
{
    *qualifiers = TQ_NONE;
    if(!ast_accept(ctx, TK_CONST))
        *qualifiers |= TQ_CONST;
	/* these do not really fit in here, and should be handled differently as it's own type, but for now this will do */
	else if(!ast_accept(ctx, TK_T_UNSIGNED))
		*qualifiers |= TQ_UNSIGNED;
    else
        return 1;
    return 0;
}

static void type_declaration_pointer(ast_context_t *ctx, ast_node_t **n)
{
	int np = 0;
	while (!ast_accept(ctx, '*'))
		++np;

	for (int i = 0; i < np; ++i)
	{
		ast_node_t* pointer_type_node = push_node(ctx, AST_POINTER_DATA_TYPE);
		pointer_type_node->data_type_data.data_type = *n;
		*n = pointer_type_node;
	}
}

static int type_declaration(ast_context_t *ctx, ast_node_t **data_type_node)
{
    *data_type_node = NULL;

    int pre_qualifiers = TQ_NONE;

    //TODO: implement const/volatile etc and other qualifiers
    type_qualifiers(ctx, &pre_qualifiers);

	struct token* tk = parse_token(&ctx->parse_context);
	if (tk->type == TK_IDENT)
	{
		ast_node_t* ref = find_type_definition(ctx, ast_token(ctx)->string);
		if (ref)
		{
			int post_qualifiers = TQ_NONE;
			type_qualifiers(ctx, &post_qualifiers);
			ast_node_t* decl_node = push_node(ctx, ref->type == AST_STRUCT_DECL || ref->type == AST_UNION_DECL ? AST_STRUCT_DATA_TYPE : AST_DATA_TYPE);
            decl_node->data_type_data.data_type = ref;
            decl_node->data_type_data.qualifiers = pre_qualifiers | post_qualifiers;
			*data_type_node = decl_node;
			parse_advance(&ctx->parse_context);
			type_declaration_pointer(ctx, data_type_node);
		}
	} else if ( !ast_accept( ctx, TK_T_CHAR ) || !ast_accept( ctx, TK_T_LONG ) || !ast_accept( ctx, TK_T_SHORT ) || !ast_accept( ctx, TK_T_INT ) ||
		 !ast_accept( ctx, TK_T_FLOAT ) || !ast_accept( ctx, TK_T_DOUBLE ) || !ast_accept(ctx, TK_T_VOID))
	{
		int primitive_type = ast_token(ctx)->type - TK_T_CHAR;
        
        int post_qualifiers = TQ_NONE;
        type_qualifiers(ctx, &post_qualifiers);

		ast_node_t* primitive_type_node = push_node( ctx, AST_PRIMITIVE );
		primitive_type_node->primitive_data.primitive_type = primitive_type;
		primitive_type_node->primitive_data.qualifiers = pre_qualifiers | post_qualifiers;
        *data_type_node = primitive_type_node;
        
        type_declaration_pointer(ctx, data_type_node);
	}
	return *data_type_node ? 0 : ( pre_qualifiers == TQ_NONE ? 0 : 1 );
}

static void expression(ast_context_t *ctx, ast_node_t **node);
static void factor( ast_context_t* ctx, ast_node_t **node );

//TODO: FIXME use a hash map or atleast hash the strings to speed it up
static ast_node_t *find_declaration(ast_context_t *ctx, const char *name)
{
    assert(ctx->function);
    for(int i = 0; i < ctx->function->func_decl_data.numdeclarations; ++i)
	{
		ast_node_t *decl = ctx->function->func_decl_data.declarations[i];
        if(!strcmp(decl->variable_decl_data.id->identifier_data.name, name))
            return decl;
	}
	for (int i = 0; i < ctx->function->func_decl_data.numparms; ++i)
	{
        if(!strcmp(ctx->function->func_decl_data.parameters[i]->variable_decl_data.id->identifier_data.name, name))
            return ctx->function->func_decl_data.parameters[i];
	}
	return NULL;
}

static ast_node_t *ident_factor(ast_context_t *ctx)
{
	const char* ident_string = ast_token(ctx)->string;
	ast_node_t* ident = identifier(ctx, ident_string);
	ast_node_t *decl = find_declaration(ctx, ident_string);
    int is_func_call = !ast_accept(ctx, '(');
    if(!decl && !is_func_call)
	{
        ast_error(ctx, "declaration not found for '%s'", ident_string);
        return NULL;
	}
	if ( is_func_call )
	{
		ast_node_t* n = push_node( ctx, AST_FUNCTION_CALL_EXPR );
		n->call_expr_data.callee = ident;
		n->call_expr_data.numargs = 0;
		do
		{
			if ( !ast_accept( ctx, ')' ) )
				break;
			ast_node_t* arg;
			expression( ctx, &arg );
			n->call_expr_data.arguments[n->call_expr_data.numargs++] = arg;
			if ( !ast_accept( ctx, ')' ) )
				break;
		} while ( !ast_accept( ctx, ',' ) );
		return n;
	}
	while(!ast_accept(ctx, '.') || !ast_accept(ctx, TK_ARROW))
    {
        int as_ptr = ast_token(ctx)->type == TK_ARROW;
		ast_node_t* n = push_node(ctx, AST_STRUCT_MEMBER_EXPR);
		n->member_expr_data.computed = 0;
		n->member_expr_data.object = ident;
		n->member_expr_data.as_pointer = as_ptr;

		// structure member access
		ast_expect(ctx, TK_IDENT, "expected structure member name");
		n->member_expr_data.property = identifier(ctx, ast_token(ctx)->string);
		ident = n;
	}
	return ident;
}

static ast_node_t *parens_factor(ast_context_t *ctx)
{
	ast_node_t* n;
	int td = type_declaration( ctx, &n );
	ast_assert( ctx, !td, "error in type" );
	if ( n )
	{
		ast_expect( ctx, ')', "no ending )" );
		ast_node_t* cast = push_node( ctx, AST_CAST );
		cast->cast_data.type = n;
		void array_subscripting( ast_context_t * ctx, ast_node_t * *node );
		array_subscripting( ctx, &cast->cast_data.expr );
		return cast;
	}
	expression( ctx, &n );
	ast_expect( ctx, ')', "no ending )" );
	return n;
}

static ast_node_t *unary_expr_factor(ast_context_t *ctx)
{
	int operator = ast_token(ctx)->type;
	ast_node_t* n;
	void array_subscripting( ast_context_t * ctx, ast_node_t * *node );
	array_subscripting( ctx, &n );
	return unary_expr( ctx, operator, 1, n );
}

static ast_node_t *integer_factor(ast_context_t *ctx)
{
	return int_literal( ctx, ast_token(ctx)->integer );
}

static ast_node_t *float_factor(ast_context_t *ctx)
{
	ast_node_t* n = push_node( ctx, AST_LITERAL );
	n->literal_data.type = LITERAL_NUMBER;
	n->literal_data.scalar = ast_token( ctx )->scalar;
    return n;
}

static ast_node_t *string_factor(ast_context_t *ctx)
{
	return string_literal( ctx, ast_token(ctx)->string );
}

static ast_node_t *sizeof_factor(ast_context_t *ctx)
{
	ast_expect( ctx, '(', "sizeof expecting (" );
	ast_node_t* so_node = push_node( ctx, AST_SIZEOF );
	int td = type_declaration( ctx, &so_node->sizeof_data.subject );
	ast_assert( ctx, !td, "error in type declaration" );
	if ( !so_node->sizeof_data.subject )
		expression( ctx, &so_node->sizeof_data.subject );
	ast_expect( ctx, ')', "sizeof expecting )" );
	return so_node;
}

typedef struct
{
    int type;
    ast_node_t *(*function)();
} ast_node_type_function_t;

static ast_node_type_function_t factors[] = {
    { TK_IDENT, ident_factor },
    { '(', parens_factor },
    { '-', unary_expr_factor },
    { '+', unary_expr_factor },
    { '!', unary_expr_factor },
    { '~', unary_expr_factor },
    { '*', unary_expr_factor },
    { '&', unary_expr_factor },
    { TK_PLUS_PLUS, unary_expr_factor },
    { TK_MINUS_MINUS, unary_expr_factor },
    { TK_INTEGER, integer_factor },
    { TK_FLOAT, float_factor },
    { TK_STRING, string_factor },
    { TK_SIZEOF, sizeof_factor }
};

static void factor( ast_context_t* ctx, ast_node_t **node )
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

static void postfix(ast_context_t *ctx, ast_node_t **node)
{
    factor(ctx,node);
	while ( !ast_accept( ctx, TK_PLUS_PLUS ) || !ast_accept( ctx, TK_MINUS_MINUS ) )
		*node = unary_expr( ctx, ast_token( ctx )->type, 0, *node );
}

void array_subscripting(ast_context_t *ctx, ast_node_t **node)
{
    postfix(ctx, node);
    while(!ast_accept(ctx, '['))
    {
        ast_node_t *rhs;
        expression(ctx, &rhs);
        ast_expect(ctx, ']', "no ending bracket for array subscript");
		*node = array_subscript_expr(ctx, *node, rhs);
    }
}

static void term(ast_context_t *ctx, ast_node_t **node)
{
    array_subscripting(ctx, node);
    
    while(!ast_accept(ctx, '/') || !ast_accept(ctx, '*') || !ast_accept(ctx, '%'))
    {
		int operator = ast_token(ctx)->type;
		ast_node_t* rhs;
		array_subscripting( ctx , &rhs );
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}


static void add_and_subtract(ast_context_t *ctx, ast_node_t **node)
{
    term(ctx, node);
    while(!ast_accept(ctx, '+') || !ast_accept(ctx, '-'))
    {
        int operator = ast_token(ctx)->type;
    	ast_node_t *rhs;
        term(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_shift(ast_context_t *ctx, ast_node_t **node)
{
    add_and_subtract(ctx, node);
    while(!ast_accept(ctx, TK_LSHIFT) || !ast_accept(ctx, TK_RSHIFT))
    {
        int operator = ast_token(ctx)->type;
    	ast_node_t *rhs;
        add_and_subtract(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void relational(ast_context_t *ctx, ast_node_t **node)
{
    bitwise_shift(ctx, node);
    while(!ast_accept(ctx, '>') || !ast_accept(ctx, '<') || !ast_accept(ctx, TK_LEQUAL) || !ast_accept(ctx, TK_GEQUAL) || !ast_accept(ctx, TK_EQUAL) || !ast_accept(ctx, TK_NOT_EQUAL))
    {
        int operator = ast_token(ctx)->type;
    	ast_node_t *rhs;
        bitwise_shift(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_and(ast_context_t *ctx, ast_node_t **node)
{
    relational(ctx, node);
    while(!ast_accept(ctx, '&'))
    {
        int operator = ast_token(ctx)->type;
    	ast_node_t *rhs;
        relational(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_xor(ast_context_t *ctx, ast_node_t **node)
{
    bitwise_and(ctx, node);
    while(!ast_accept(ctx, '^'))
    {
        int operator = ast_token(ctx)->type;
    	ast_node_t *rhs;
        bitwise_and(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void bitwise_or(ast_context_t *ctx, ast_node_t **node)
{
    bitwise_xor(ctx, node);
    while(!ast_accept(ctx, '|'))
    {
        int operator = ast_token(ctx)->type;
    	ast_node_t *rhs;
        bitwise_xor(ctx, &rhs);
        *node = bin_expr(ctx, operator, *node, rhs);
    }
}

static void ternary(ast_context_t *ctx, ast_node_t **node)
{
    bitwise_or(ctx, node);
    
    while(!ast_accept(ctx, '?'))
    {
    	ast_node_t *consequent, *alternative, *ternary_node;
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

static bool accept_assigment_operator(ast_context_t *ctx, int *which)
{
	static const int assignment_operators[] = {
		'=',		   TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_DIVIDE_ASSIGN, TK_MULTIPLY_ASSIGN,
		TK_MOD_ASSIGN, TK_AND_ASSIGN,  TK_OR_ASSIGN,	TK_XOR_ASSIGN};
	for (int i = 0; i < COUNT_OF(assignment_operators); ++i)
	{
		if (!ast_accept(ctx, assignment_operators[i]))
		{
			if (which)
				*which = assignment_operators[i];
			return true;
		}
	}
	return false;
}

static void regular_assignment(ast_context_t *ctx, ast_node_t **node)
{
	int operator;
	ast_node_t *a, *b;

	ternary(ctx, &a);
	
	if(!accept_assigment_operator(ctx, &operator))
	{
		//no assignment
		*node = a;
		return;
	}

	ast_node_t* root_node = assignment_expr(ctx, operator, a, NULL);
	ast_node_t* n = root_node;
	while (1)
	{
		ternary(ctx, &b);
		if(!accept_assigment_operator(ctx, &operator))
		{
			n->assignment_expr_data.rhs = b;
			break;
		}
		ast_node_t *old = n;
		n = assignment_expr(ctx, operator, b, NULL);
		old->assignment_expr_data.rhs = n;
	}

	*node = root_node;
}

static void expression( ast_context_t* ctx, ast_node_t** node )
{
	regular_assignment( ctx, node );
}

void expression_sequence(ast_context_t *ctx, ast_node_t **node)
{
	ast_node_t* seq = push_node( ctx, AST_SEQ_EXPR );
	seq->seq_expr_data.numexpr = 0;
	ast_node_t* n;
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
	#ifndef _WIN32
		printf("\x1B[%dm", 31 + (n % 7));
	#endif
}

static void reset_print_color()
{
	#ifndef _WIN32
		printf("\x1B[0m");
	#endif
}

void print_ast(ast_node_t *n, int depth)
{
	// printf("(node type %s)\n", ast_node_type_t_to_string(n->type));

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
		linked_list_reversed_foreach( n->block_stmt_data.body, ast_node_t**, it, { print_ast( *it, depth + 1 ); } );
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
		linked_list_reversed_foreach( n->program_data.body, ast_node_t**, it, { print_ast( *it, depth + 1 ); } );
        break;
    case AST_FUNCTION_DECL:
	{
        
		if(n->func_decl_data.body != NULL)
		{
			printf("function '%s'\n", n->func_decl_data.id->identifier_data.name);
			for(int i = 0; i < n->func_decl_data.numparms; ++i)
			{
				print_tabs(depth);
				printf("parm %d -> %s\n", i, n->func_decl_data.parameters[i]->variable_decl_data.id->identifier_data.name);
				print_ast(n->func_decl_data.parameters[i]->variable_decl_data.data_type, depth + 1);
			}
			print_ast(n->func_decl_data.body, depth + 1);
		} else
		{
			printf("import function '%s'\n", n->func_decl_data.id->identifier_data.name);
		}
	} break;

    case AST_POINTER_DATA_TYPE:
	{
        printf("pointer data type\n");
        print_ast(n->data_type_data.data_type, depth + 1);
	} break;

	case AST_FUNCTION_CALL_EXPR:
    {
        ast_node_t **args = n->call_expr_data.arguments;
        int numargs = n->call_expr_data.numargs;
        ast_node_t *callee = n->call_expr_data.callee;
        assert(callee->type == AST_IDENTIFIER);
        //TODO: handle other callee types
        printf("function call expression '%s'\n", callee->identifier_data.name);
        for(int i = 0; i < numargs; ++i)
		{
			printf("\targ %d: %s\n", i, ast_node_type_t_to_string(args[i]->type));
			print_ast(args[i], depth + 1);
		}
	} break;

    case AST_IF_STMT:
    {
        ast_node_t *test = n->if_stmt_data.test;
        ast_node_t *consequent = n->if_stmt_data.consequent;
        printf("if statement\n");
        print_ast(test, depth + 1);
        print_ast(consequent, depth + 1);
    } break;

	case AST_WHILE_STMT:
	{
		ast_node_t* test = n->while_stmt_data.test;
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
        printf("array data type [%d]\n", n->data_type_data.array_size);
        print_ast(n->data_type_data.data_type, depth + 1);
	} break;

    case AST_PRIMITIVE:
	{
        printf("type %s\n", data_type_strings[n->primitive_data.primitive_type]);
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

    case AST_CAST:
	{
        printf("cast\n");
        print_ast(n->cast_data.type, depth + 1);
        print_ast(n->cast_data.expr, depth + 1);
	} break;

	case AST_SIZEOF:
	{
        printf("sizeof\n");
        print_ast(n->sizeof_data.subject, depth + 1);
	} break;
	
	case AST_STRUCT_DATA_TYPE:
	{
		int is_union = n->data_type_data.data_type->type == AST_UNION_DECL;
		printf("%s %s\n", is_union ? "union" : "struct", n->data_type_data.data_type->struct_decl_data.name);
	} break;
	
	case AST_STRUCT_MEMBER_EXPR:
	{
		ast_node_t *ob = n->member_expr_data.object;
		ast_node_t *prop = n->member_expr_data.property;
		int arrow = n->member_expr_data.as_pointer;
		printf("struct member expression %s\n", arrow ? "->" : ".");
		print_ast(ob, depth + 1);
		print_ast(prop, depth + 1);
	} break;
    
	default:
		printf("unhandled type %s | %s:%d\n", ast_node_type_t_to_string(n->type), __FILE__, __LINE__);
		break;
    }
    set_print_color(depth);
    if(print_lines)
        printf("-------end of depth %d--------------\n", depth);
    reset_print_color();
}

static ast_node_t *handle_variable_declaration( ast_context_t* ctx, ast_node_t* type_decl, ast_node_t* id, int is_param )
{
	ast_node_t* decl_node = push_node(ctx, AST_VARIABLE_DECL);
	if (!is_param && ctx->function)
	{
		ctx->function->func_decl_data.declarations[ctx->function->func_decl_data.numdeclarations++] = decl_node;
	}
	decl_node->variable_decl_data.id = id;
	decl_node->variable_decl_data.data_type = type_decl;
	decl_node->variable_decl_data.initializer_value = NULL;

	if ( !ast_accept( ctx, '[' ) )
	{
		ast_node_t* array_type_node = push_node( ctx, AST_ARRAY_DATA_TYPE );
		decl_node->variable_decl_data.data_type =
			array_type_node; // set our first array type on the declaration node it's data type
		while ( 1 )
		{
			ast_expect(ctx, TK_INTEGER, "expected constant int array size");
			int dc = ast_token(ctx)->integer.value;
			ast_assert( ctx, dc > 0, "array size can't be zero" );
			ast_expect(ctx, ']', "expected ] after array type declaration");
			array_type_node->data_type_data.array_size = dc;
			array_type_node->data_type_data.data_type = type_decl;
			if ( ast_accept( ctx, '[' ) )
				break;
			ast_node_t* new_node = push_node( ctx, AST_ARRAY_DATA_TYPE );
			array_type_node->data_type_data.data_type = new_node;
			array_type_node = new_node;
		}
	}

	//initializer value
	if(!ast_accept(ctx, '='))
	{
		ast_node_t *initializer_value;
		expression(ctx, &initializer_value);
		ast_assert(ctx, initializer_value, "expected initializer value after =");
		decl_node->variable_decl_data.initializer_value = initializer_value;
	}
	return decl_node;
}

static void variable_declaration( ast_context_t* ctx, ast_node_t** out_decl_node, int is_param )
{
	ast_node_t* type_decl = NULL;
	int td = type_declaration(ctx, &type_decl);
    ast_assert(ctx, !td, "error in type declaration");
	if(type_decl)
    {
        ast_expect(ctx, TK_IDENT, "expected identifier for type declaration");
		ast_node_t* id = identifier( ctx, ast_token(ctx)->string );
		*out_decl_node = handle_variable_declaration(ctx, type_decl, id, is_param);
        return;
	}
    *out_decl_node = NULL;
}

static ast_node_t *init_statement(ast_context_t *ctx)
{
	ast_node_t* decl_node;
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
		ast_node_t* seq = push_node( ctx, AST_SEQ_EXPR );
		seq->seq_expr_data.numexpr = 0;
		seq->seq_expr_data.expr[seq->seq_expr_data.numexpr++] = decl_node;
		do
		{
            ast_node_t *n;
			expression( ctx, &n );
			seq->seq_expr_data.expr[seq->seq_expr_data.numexpr++] = n;
		} while(!ast_accept(ctx, ','));
        return seq;
	}
	return decl_node;
}

static ast_node_t *block_statement(ast_context_t *ctx)
{
	ast_node_t* n = push_node( ctx, AST_BLOCK_STMT );
	n->block_stmt_data.body = linked_list_create_with_custom_allocator(void*, ctx->allocator, arena_alloc);

	while ( 1 )
	{
		if ( !ast_accept( ctx, '}' ) )
			break;
		ast_node_t* stmt;
		statement( ctx, &stmt );
		ast_assert( ctx, stmt, "expected statement" );
		linked_list_prepend( n->block_stmt_data.body, stmt );
	}
	return n;
}

static ast_node_t* emit_statement( ast_context_t* ctx )
{
	ast_expect( ctx, TK_INTEGER, "expected integer after emit statement" );
	int opcode = ast_token(ctx)->integer.value;
	ast_node_t* n = push_node( ctx, AST_EMIT );
	n->emit_data.opcode = opcode;
	return n;
}

static ast_node_t* if_statement( ast_context_t* ctx )
{
	ast_expect( ctx, '(', "expected ( after if" );
	ast_node_t *test, *body, *if_node;
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

static ast_node_t* break_statement( ast_context_t* ctx )
{
	// TODO: maybe move this to iteration only statements function
	return push_node( ctx, AST_BREAK_STMT );
}

static ast_node_t* while_statement( ast_context_t* ctx )
{
	ast_expect( ctx, '(', "expected ( after while\n" );

	ast_node_t* test, *body, *while_node;
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

static ast_node_t* do_statement( ast_context_t* ctx )
{
	ast_node_t* test, *body, *node;
    
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

static ast_node_t* for_statement( ast_context_t* ctx )
{
	ast_expect( ctx, '(', "expected ( after for" );
	ast_node_t *init, *test, *update;
	init = test = update = NULL;

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

	ast_node_t* body;
	statement( ctx, &body );
	ast_assert( ctx, body, "no body for for statement" );
	ast_node_t* for_node = push_node( ctx, AST_FOR_STMT );
	for_node->for_stmt_data.init = init;
	for_node->for_stmt_data.test = test;
	for_node->for_stmt_data.update = update;
	for_node->for_stmt_data.body = body;
	return for_node;
}

static ast_node_t* return_statement( ast_context_t* ctx )
{
	ast_node_t* ret_stmt = push_node( ctx, AST_RETURN_STMT );
	ret_stmt->return_stmt_data.argument = NULL;
	if ( !ast_accept( ctx, ';' ) )
		return ret_stmt;
	expression( ctx, &ret_stmt->return_stmt_data.argument );
	return ret_stmt;
}

static ast_node_type_function_t statements[] = {
    { TK_EMIT, emit_statement },
    { TK_IF, if_statement },
    { TK_BREAK, break_statement },
    { TK_WHILE, while_statement },
    { TK_DO, do_statement },
    { TK_FOR, for_statement },
    { TK_RETURN, return_statement },
    { '{', block_statement }
};

static void statement_node(ast_context_t *ctx, ast_node_t **node)
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

static void statement(ast_context_t *ctx, ast_node_t **node)
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

static void handle_typedef(ast_context_t *ctx)
{    
    ast_node_t typedef_node = {.parent = NULL, .type = AST_TYPEDEF, .rvalue = 0};
    ast_node_t* type_decl = NULL;

    int td = type_declaration( ctx , &type_decl );
    ast_assert(ctx, !td, "error in type declaration for typedef");
    if ( !type_decl )
        ast_error( ctx, "expected type for typedef, got '%s'", token_type_to_string( parse_token(&ctx->parse_context)->type ) );
    typedef_node.typedef_data.type = type_decl;
    ast_expect(ctx, TK_IDENT, "expected name for typedef");
    snprintf(typedef_node.typedef_data.name, sizeof(typedef_node.typedef_data.name), "%s",
             ast_token(ctx)->string);
    ast_expect(ctx, ';', "no ending semicolon for typedef");
    add_type_definition(ctx, typedef_node.typedef_data.name, &typedef_node);
}

static void handle_enum_declaration(ast_context_t *ctx)
{    
    ast_node_t enum_node = {.parent = NULL, .type = AST_ENUM, .rvalue = 0};
    if(ast_accept(ctx, '{'))
	{
		ast_expect(ctx, TK_IDENT, "expected name for enum");
		snprintf(enum_node.enum_data.name, sizeof(enum_node.enum_data.name), "%s", ast_token(ctx)->string);
        ast_expect(ctx, '{', "missing {");
	} else
	{
		snprintf(enum_node.enum_data.name, sizeof(enum_node.enum_data.name), "#enum_%d", ctx->numtypes);
	}

    enum_node.enum_data.numvalues = 0;
    
    int currentvalue = 0;
    do
    {
		ast_expect(ctx, TK_IDENT, "expected value for enum '%s'", enum_node.enum_data.name);
		ast_node_t* enum_value_node = push_node(ctx, AST_ENUM_VALUE);
		snprintf(enum_value_node->enum_value_data.ident, sizeof(enum_value_node->enum_value_data.ident), "%s",
				 ast_token(ctx)->string);
		if(!ast_accept(ctx, '='))
        {
            ast_expect(ctx, TK_INTEGER, "expected integer for enum value '%s'\n", enum_node.enum_data.name);
            currentvalue = ast_token(ctx)->integer.value;
        }
        enum_value_node->enum_value_data.value = currentvalue;

		// add each enum value as type itself aswell, so we can access it easy
        //TODO: FIX atm type isn't recognized because it's not a declaration of a variable
		add_type_definition(ctx, enum_value_node->enum_value_data.ident, enum_value_node);
		enum_node.enum_data.values[enum_node.enum_data.numvalues++] = enum_value_node;
    } while(!ast_accept(ctx, ','));
    
    ast_expect(ctx, '}', "missing }");
    ast_expect(ctx, ';', "no ending semicolon for typedef");
    add_type_definition(ctx, enum_node.enum_data.name, &enum_node);
}

static void handle_struct_or_union_declaration(ast_context_t *ctx)
{
    int is_union_type = ast_token(ctx)->type == TK_UNION;
    const char *type_string = is_union_type ? "union" : "struct";

    ast_node_t struct_node = {.parent = NULL, .type = is_union_type ? AST_UNION_DECL : AST_STRUCT_DECL, .rvalue = 0};			
    struct_node.struct_decl_data.numfields = 0;
    if(ast_accept(ctx, '{'))
    {
        ast_expect(ctx, TK_IDENT, "no name for %s type", type_string);
        snprintf(struct_node.struct_decl_data.name, sizeof(struct_node.struct_decl_data.name), "%s",
                 ast_token(ctx)->string);

        ast_expect(ctx, '{', "no starting brace for %s type", type_string);
    } else {
        //if no name is specified set a random name
        snprintf(struct_node.struct_decl_data.name, sizeof(struct_node.struct_decl_data.name), "#%s_%d", type_string, ctx->numtypes);
    }

    while (1)
    {
        ast_node_t* field_node;
        variable_declaration(ctx, &field_node, 0);
        if (!field_node)
            break;
        ast_expect(ctx, ';', "expected ; in %s field", type_string);
        struct_node.struct_decl_data.fields[struct_node.struct_decl_data.numfields++] = field_node;
    }

    ast_expect(ctx, '}', "no ending brace for %s type", type_string);
    ast_expect(ctx, ';', "no ending semicolon for %s type", type_string);

    //linked_list_prepend(ctx->program_node->program_data.body, struct_node);
    add_type_definition(ctx, struct_node.struct_decl_data.name, &struct_node);
}

static void handle_function_definition(ast_context_t *ctx, ast_node_t *type_decl, ast_node_t *id)
{
	if ( !type_decl )
		ast_error( ctx, "expected function return type got '%s'", token_type_to_string( parse_token(&ctx->parse_context)->type ) );
	ast_node_t* decl = push_node( ctx, AST_FUNCTION_DECL );
	decl->func_decl_data.return_data_type = type_decl;
	decl->func_decl_data.numparms = 0;
	decl->func_decl_data.variadic = 0;
	decl->func_decl_data.numdeclarations = 0;
	decl->func_decl_data.id = id;
	ctx->function = decl;
	//ast_expect( ctx, '(', "expected ( after function" );

	ast_node_t* parm_decl = NULL;
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

	ast_node_t* block_node = NULL;
	//check if it's just a forward decl
	if (ast_accept(ctx, ';'))
	{
		statement_node(ctx, &block_node);
		ast_assert(ctx, block_node->type == AST_BLOCK_STMT, "expected { after function");
	}
	linked_list_prepend( ctx->program_node->program_data.body, decl );
	ctx->function = ctx->default_function;
	decl->func_decl_data.body = block_node;
}

static bool handle_function_definition_or_variable_declaration(ast_context_t *ctx)
{
	ast_node_t* type_decl = NULL;
	int td = type_declaration( ctx , &type_decl );
	ast_assert( ctx, !td, "error in type declaration" );
	if(!type_decl)
	{
		return false;
	}
	// TODO: implement global variables assignment, function prototypes and a preprocessor

	ast_expect( ctx, TK_IDENT, "expected ident" );
	ast_node_t* id = identifier( ctx, ast_token(ctx)->string );
	
	if(!ast_accept(ctx, '('))
	{
		handle_function_definition(ctx, type_decl, id);
		return true;
	}
	ast_node_t *variable_decl = handle_variable_declaration(ctx, type_decl, id, 0);
	linked_list_prepend( ctx->program_node->program_data.body, variable_decl );
	ast_expect( ctx, ';', "missing ;" );
	return true;
}

static void program(ast_context_t *ctx)
{
    while(1)
    {
        if(!ast_accept(ctx, TK_EOF)) break;
		
		if(!ast_accept(ctx, TK_TYPEDEF))
		{
            handle_typedef(ctx);
			continue;
		} else if(!ast_accept(ctx, TK_STRUCT) || !ast_accept(ctx, TK_UNION))
		{
			handle_struct_or_union_declaration(ctx);
            continue;
		} else if(!ast_accept(ctx, TK_ENUM))
        {
            handle_enum_declaration(ctx);
            continue;
        } else
		{
			if(!handle_function_definition_or_variable_declaration(ctx))
			{
				//ast_node_t* stmt;
				//statement( ctx, &stmt );
				//linked_list_prepend( ctx->program_node->program_data.body, stmt );
				ast_error(ctx, "expected function or global variable declaration");
			}
		}
	}
}

static void visit_ast_node(traverse_context_t *ctx, ast_node_t *n)
{
    if (ctx->single_result)
    {
        ctx->visiteestacksize = (ctx->visiteestacksize + 1) % COUNT_OF(ctx->visiteestack);
        ctx->visiteestack[ctx->visiteestacksize] = n;
    }
	// printf("n=%s\n", ast_node_type_t_to_string(n->type));
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
	if(!n)
		return;
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

static int ast_filter_node(ast_node_t* n, ast_node_t *node)
{
    if (n == node)
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

ast_node_t* ast_tree_node_by_node(traverse_context_t* ctx, ast_node_t* head, ast_node_t *node)
{
    ctx->single_result = 1;
    return ast_tree_traverse(ctx, head, ast_filter_node, node);
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

bool ast_process_tokens(ast_context_t* ctx, struct token* tokens, int num_tokens)
{
	ctx->function = ctx->default_function;
    ctx->parse_context.current_token = NULL;
    ctx->parse_context.num_tokens = num_tokens;
    ctx->parse_context.token_index = 0;
    ctx->parse_context.tokens = tokens;

    if(setjmp(ctx->jmp))
    {
		return false;
    }
    
    program(ctx);
	return true;
}
