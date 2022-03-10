#include "std.h"
#include "codegen.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"

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

static int fundamental_type_size(compiler_t *ctx, int type)
{
    switch(type)
	{
    case DT_CHAR: return ctx->fts.charsize;
    case DT_SHORT: return ctx->fts.shortsize;
    case DT_INT: return ctx->fts.intsize;
    case DT_LONG: return ctx->fts.longsize;
    case DT_NUMBER: return ctx->fts.floatsize;
    case DT_FLOAT: return ctx->fts.floatsize;
    case DT_DOUBLE: return ctx->fts.doublesize;
    case DT_VOID: return 0;
	}
    debug_printf("unhandled type %d\n", type);
    return 0;
}

static int data_type_size(compiler_t *ctx, struct ast_node *n)
{
    switch(n->type)
	{
    case AST_STRUCT_DATA_TYPE:
    {
        int total = 0;
        struct ast_node *ref = n->data_type_data.data_type;
        assert(ref);
		if(ref->type == AST_UNION_DECL)
		{				
			for(int i = 0; i < ref->struct_decl_data.numfields; ++i)
			{
				int tmp = data_type_size(ctx, ref->struct_decl_data.fields[i]->variable_decl_data.data_type);
				if(tmp > total)
					total = tmp;
			}
		} else if(ref->type == AST_STRUCT_DECL)
		{
			for(int i = 0; i < ref->struct_decl_data.numfields; ++i)
				total += data_type_size(ctx, ref->struct_decl_data.fields[i]->variable_decl_data.data_type);
		} else
		{
			debug_printf( "unhandled struct data type node '%s', can't get size\n", AST_NODE_TYPE_to_string( ref->type ) );
		}
        return total;
	} break;
    case AST_IDENTIFIER:
	{
		struct variable* var = hash_map_find( ctx->function->variables, n->identifier_data.name );
		assert( var );
        return data_type_size(ctx, var->data_type_node);
	}
	break;
	case AST_DATA_TYPE:
		return data_type_size(ctx, n->data_type_data.data_type);
	case AST_POINTER_DATA_TYPE:
        return ctx->fts.pointersize;
    case AST_PRIMITIVE:
        return fundamental_type_size(ctx, n->primitive_data.primitive_type);
    case AST_ARRAY_DATA_TYPE:
	{
		assert( n->data_type_data.array_size > 0 );

		if ( n->data_type_data.data_type->type == AST_ARRAY_DATA_TYPE )
			return data_type_size( ctx, n->data_type_data.data_type ) * n->data_type_data.array_size;
		else if ( n->data_type_data.data_type->type == AST_PRIMITIVE )
		{
            //printf("array size = %d, primitive_type_size = %d\n", n->data_type_data.array_size, fundamental_type_size( ctx, n->data_type_data.data_type->primitive_data_type_data.primitive_type ));
			return fundamental_type_size( ctx, n->data_type_data.data_type->primitive_data.primitive_type ) *
				   n->data_type_data.array_size;
		}
		else
		{
            perror("unhandled else for data_type_size\n");
            return 0;
		}
	} break;
	}
	debug_printf( "unhandled data type node '%s', can't get size\n", AST_NODE_TYPE_to_string( n->type ) );
	return 0;
}

typedef enum
{
	VD_INVALID,
	VD_CONSTANT, //just value stored in data constant
	VD_REGISTER, //the value is stored in a register e.g inside of VREG32 which could map to EAX
	VD_REGISTER_ADDRESS, //the value is stored on the location that the register points to e.g EAX points to the location of where the value resides
	//mov ebx, [eax]
	VD_REGISTER_ADDRESS_OFFSET //same as above except the location is offset e.g mov ebx, [eax + offset]
} value_data_t;

static const char *value_data_strings[] = {"invalid","constant","register","register address","register address offset",NULL};

typedef enum
{
	VC_LVALUE,
	VC_RVALUE,
	VC_TVALUE
} value_category_t;

static const char *value_category_strings[] = {"lvalue","rvalue","tvalue",NULL};

typedef struct
{
	value_category_t category;
	value_data_t data_type;
	int nbits;
	union
	{
		vregval_t value;
		struct
		{
			vreg_t reg;
			int offset;
		};
	} data;
} value_t;

typedef struct
{
	int ast_node_type;
	void (*proc)(compiler_t*,ast_node_t*,value_t*);
} value_key_proc_pair_t;

static void rvalue_literal(compiler_t *ctx, ast_node_t *n, value_t *v)
{
	v->data_type = VD_CONSTANT;
	switch ( n->literal_data.type )
	{
		case LITERAL_INTEGER:
			setvregval(&v->data.value, n->literal_data.integer);
			break;
		case LITERAL_STRING:
		{
			int idx = add_indexed_data(ctx, n->literal_data.string, strlen(n->literal_data.string) + 1);
			setvregvalindex(&v->data.value, idx);
		} break;
		default:
			perror( "unhandled literal" );
		break;
	}
	v->nbits = v->data.value.nbits;
}

static value_key_proc_pair_t rvalue_key_proc_pairs[] = {
	{AST_LITERAL, rvalue_literal},
	//{AST_IDENTIFIER, rvalue_identifier},
	{AST_NONE, NULL}
};

static void lvalue_identifier(compiler_t *ctx, ast_node_t *n, value_t *v)
{
	const char* variable_name = n->identifier_data.name;
	struct variable* var = hash_map_find( ctx->function->variables, variable_name );
	assert( var );
	struct ast_node *variable_type = var->data_type_node;
	//int offset = var->is_param ? 4 + var->offset : 0xff - var->offset + 1;
	v->nbits = data_type_size(ctx, variable_type);
	printf("%s nbits=%d\n", variable_name, v->nbits);
}

static value_key_proc_pair_t lvalue_key_proc_pairs[] = {
	{AST_IDENTIFIER, lvalue_identifier},
	{AST_NONE, NULL}
};

static void rvalue(compiler_t *ctx, ast_node_t *n, value_t *v)
{
	v->category = VC_RVALUE;
	v->data_type = VD_INVALID;
	v->nbits = 0;
	for(int i = 0; rvalue_key_proc_pairs[i].proc; ++i)
	{
		if(rvalue_key_proc_pairs[i].ast_node_type == n->type)
		{
			return rvalue_key_proc_pairs[i].proc(ctx, n, v);
		}
	}
}

static void lvalue(compiler_t *ctx, ast_node_t *n, value_t *v)
{
	v->category = VC_LVALUE;
	v->data_type = VD_INVALID;
	v->nbits = 0;
	for(int i = 0; lvalue_key_proc_pairs[i].proc; ++i)
	{
		if(lvalue_key_proc_pairs[i].ast_node_type == n->type)
		{
			return lvalue_key_proc_pairs[i].proc(ctx, n, v);
		}
	}
}

static void print_value(value_t *v)
{
	printf("value type:%s,category:%s,nbits:%d\n", value_data_strings[v->data_type], value_category_strings[v->category], v->nbits);
}

int process(compiler_t* ctx, ast_node_t* n)
{
	switch (n->type)
    {
		case AST_PROGRAM:			
			linked_list_reversed_foreach( n->program_data.body, struct ast_node**, it, {
				process(ctx, (*it));
			} );
		break;
        case AST_BIN_EXPR:
        {
			value_t lhs_value, rhs_value;
			rvalue(ctx, n->bin_expr_data.lhs, &lhs_value);
			rvalue(ctx, n->bin_expr_data.rhs, &rhs_value);
			
			print_value(&lhs_value);
			print_value(&rhs_value);
        } break;
		
		case AST_ASSIGNMENT_EXPR:
		{
			value_t lhs_value, rhs_value;
			rvalue(ctx, n->assignment_expr_data.rhs, &rhs_value);
			lvalue(ctx, n->assignment_expr_data.lhs, &lhs_value);
			
			//TODO: FIXME
		} break;
		
		case AST_VARIABLE_DECL:
		{
			struct ast_node *id = n->variable_decl_data.id;
			struct ast_node *data_type_node = n->variable_decl_data.data_type;
			struct ast_node *iv = n->variable_decl_data.initializer_value;
			assert(id->type == AST_IDENTIFIER);
			const char *variable_name = id->identifier_data.name;
			
			int variable_size = data_type_size(ctx, data_type_node);
			assert(variable_size > 0);
			if(ctx->function)
			{
				ctx->function->localvariablesize += variable_size;

				int offset = ctx->function->localvariablesize;
				
				struct variable tv = { .offset = offset, .is_param = 0, .data_type_node = data_type_node };
				hash_map_insert( ctx->function->variables, variable_name, tv );
			} else
				printf("TODO FIXME global variables\n");
			if(iv)
			{
				struct ast_node c = { .type = AST_ASSIGNMENT_EXPR };
				c.assignment_expr_data.lhs = id;
				c.assignment_expr_data.rhs = iv;
				c.assignment_expr_data.operator = '=';
				process(ctx, &c);
			}
		} break;
		
		default:
		printf("unhandled type %s | %s:%d\n", AST_NODE_TYPE_to_string(n->type), __FILE__, __LINE__);
		break;
    }
}

int codegen(compiler_t* ctx, ast_node_t *head)
{
	ctx->fts.longsize = 64;
	ctx->fts.intsize = 32;
	ctx->fts.shortsize = 16;
	ctx->fts.charsize = 8;
	ctx->fts.floatsize = 32;
	ctx->fts.doublesize = 64;
	ctx->fts.pointersize = 64;
    ctx->functions = linked_list_create(struct function);
	
	struct function func = {
		.location = 0,
		.name = "_start",
		.localvariablesize = 0,
		.variables = hash_map_create(struct variable)
	};
	ctx->function = linked_list_prepend(ctx->functions, func);
    process(ctx, head);
    return 0;
}
