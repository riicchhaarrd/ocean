#include "std.h"
#include "ast.h"
#include "compile.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"
#include "codegen_targets.h"
//gcc -w -g test.c compile.c ast.c lex.c parse.c && ./a.out

int add_indexed_data(compiler_t *ctx, const void *buffer, size_t len)
{
	if(ctx->numindexeddata + 1 >= MAX_INDEXED_DATA)
		perror("out of memory for indexed data");
	indexed_data_t *id = &ctx->indexed_data[ctx->numindexeddata++];
	id->index = ctx->numindexeddata - 1;
	id->length = len;
	id->buffer = buffer;
	return id->index;
}

function_t *compiler_alloc_function(compiler_t *ctx, const char *name)
{
	function_t gv;
	snprintf(gv.name, sizeof(gv.name), "%s", name);
	gv.localvariablesize = 0;
	//TODO: free/cleanup variables
	gv.variables = hash_map_create_with_custom_allocator(variable_t, ctx->allocator, arena_alloc);
	gv.bytecode = NULL;
	hash_map_insert(ctx->functions, name, gv);
	
	//TODO: FIXME make insert return a reference to the data inserted instead of having to find it again.
	return hash_map_find(ctx->functions, name);
}

void compiler_init(compiler_t *c, arena_t *allocator, int numbits)
{
	assert(numbits == 64);
	
	memset(c, 0, sizeof(compiler_t));
	
	//x64
	c->fts.longsize = 64;
	c->fts.intsize = 32;
	c->fts.shortsize = 16;
	c->fts.charsize = 8;
	c->fts.floatsize = 32;
	c->fts.doublesize = 64;
	c->fts.pointersize = 64;
	
	codegen_x64(&c->cg);
	
	c->numbits = numbits;
	c->allocator = allocator;
	c->functions = hash_map_create_with_custom_allocator(function_t, c->allocator, arena_alloc);
	
	//mainly just holder for global variables and maybe code without function
	c->function = compiler_alloc_function(c, "_global_variables");
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
		variable_t* var = hash_map_find( ctx->function->variables, n->identifier_data.name );
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
			printf("literal integer: %d\n", n->literal_data.integer);
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
	printf("literal %d bits\n", v->nbits);
}

void rvalue(compiler_t *ctx, ast_node_t *n, value_t *v);

static void rvalue_bin_expr(compiler_t *ctx, ast_node_t *n, value_t *v)
{
	value_t lhs_value, rhs_value;
	rvalue(ctx, n->bin_expr_data.lhs, &lhs_value);
	rvalue(ctx, n->bin_expr_data.rhs, &rhs_value);
	
	if(lhs_value.data_type == VD_CONSTANT && rhs_value.data_type == VD_CONSTANT)
	{
		printf("bin expr: %d %c %d\n", getvregval(&lhs_value.data.value), n->bin_expr_data.operator, getvregval(&rhs_value.data.value));
		v->data_type = VD_CONSTANT;
		//pick the side with more bits
		v->nbits = lhs_value.data.value.nbits > rhs_value.data.value.nbits ? lhs_value.data.value.nbits : rhs_value.data.value.nbits;
		switch(n->bin_expr_data.operator)
		{
			case '+': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  + getvregval(&rhs_value.data.value)); break;
			case '-': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  - getvregval(&rhs_value.data.value)); break;
			case '/': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  / getvregval(&rhs_value.data.value)); break;
			case '*': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  * getvregval(&rhs_value.data.value)); break;
			case '%': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  % getvregval(&rhs_value.data.value)); break;
			case '^': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  ^ getvregval(&rhs_value.data.value)); break;
			case '|': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  | getvregval(&rhs_value.data.value)); break;
			case '&': setvregval(&v->data.value, getvregval(&lhs_value.data.value)
				  & getvregval(&rhs_value.data.value)); break;
			default:
				perror("unsupported\n");
			break;
		}
	} else
	{
		//TODO: FIXME
	}
}

static value_key_proc_pair_t rvalue_key_proc_pairs[] = {
	{AST_LITERAL, rvalue_literal},
	{AST_BIN_EXPR, rvalue_bin_expr},
	//{AST_IDENTIFIER, rvalue_identifier},
	{AST_NONE, NULL}
};

static void lvalue_identifier(compiler_t *ctx, ast_node_t *n, value_t *v)
{
	const char* variable_name = n->identifier_data.name;
	variable_t* var = hash_map_find( ctx->function->variables, variable_name );
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

void rvalue(compiler_t *ctx, ast_node_t *n, value_t *v)
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

#define compiler_assert(ctx, expr, ...) \
    compiler_assert_r(ctx, (intptr_t)expr, #expr, ## __VA_ARGS__)
	
static void compiler_assert_r(compiler_t *ctx, int expr, const char *expr_str, const char *fmt, ...)
{
    if(expr)
        return;

	char buffer[512] = { 0 };
	va_list va;
	va_start( va, fmt );
	vsnprintf( buffer, sizeof( buffer ), fmt, va );
	debug_printf( "compiler assert failed: '%s' %s\n", expr_str, buffer );
	va_end( va );

	longjmp(ctx->jmp, 1);
}

static void vreg_map(compiler_t *ctx, vreg_t *out, vreg_t reg)
{
	if(reg == VREG_ANY)
	{
		reg = VREG_0;
		for(int i = 0; i < 4; ++i)
		{
			if(ctx->vregister_usage[VREG_0 + i] <= ctx->vregister_usage[reg])
				reg = VREG_0 + i;
		}
	}
	if(ctx->vregister_usage[reg] > 0)
	{
		ctx->cg.push(ctx, reg);
	}
	++ctx->vregister_usage[reg];
	*out = reg;
}

static void vreg_unmap(compiler_t *ctx, vreg_t *regptr)
{
	vreg_t reg = *regptr;
	
	if(ctx->vregister_usage[reg] > 0)
	{
		ctx->cg.pop(ctx, reg);
		--ctx->vregister_usage[reg];
	}
}

vreg_t process(compiler_t* ctx, ast_node_t* n)
{
	codegen_t *cg = &ctx->cg;
	printf("n->type=%s\n",AST_NODE_TYPE_to_string(n->type));
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
			
			vreg_t a, b, c;
			vreg_map(ctx, &a, VREG_ANY);
			vreg_map(ctx, &b, VREG_ANY);
			
			cg->mov_r_imm32(ctx, a, getvregval(&lhs_value.data.value), NULL);
			cg->mov_r_imm32(ctx, b, getvregval(&rhs_value.data.value), NULL);
			c = cg->add(ctx, a, b);
			//do something with result in vreg c
			vreg_unmap(ctx, &a);
			vreg_unmap(ctx, &b);
			return c;
        } break;
		
		case AST_ASSIGNMENT_EXPR:
		{
			value_t lhs_value, rhs_value;
			rvalue(ctx, n->assignment_expr_data.rhs, &rhs_value);
			lvalue(ctx, n->assignment_expr_data.lhs, &lhs_value);
			
			//TODO: FIXME
			printf("setting value to %d\n", getvregval(&rhs_value.data.value));
		} break;
		
		case AST_BLOCK_STMT:
		{
			//TODO: stack of scopes
			linked_list_reversed_foreach(n->block_stmt_data.body, struct ast_node**, it,
			{
				process(ctx, *it);
			});
		} break;
		
		case AST_FUNCTION_DECL:
		{
			if (n->func_decl_data.body)
			{
				const char *function_name = n->func_decl_data.id->identifier_data.name;
				function_t *oldfunc = ctx->function;
				function_t *tmp = hash_map_find(ctx->functions, function_name);
				compiler_assert(ctx, !tmp, "function already exists '%s'", function_name);
				function_t *func = compiler_alloc_function(ctx, function_name);
				ctx->function = func;
				//TODO: FIXME can't add any new functions to the functions hash map or otherwise ctx->function gets invalidated
				//maybe store function name instead
				
				//TODO: FIXME
				
				
				//add up all the variables in the function's scope and calculate how much space in bytes we need to allocate				
				traverse_context_t traverse_ctx = { 0 };

				ast_node_t* variable_declarations[32];
				size_t num_variable_declarations = ast_tree_nodes_by_type(&traverse_ctx, n->func_decl_data.body, AST_VARIABLE_DECL, &variable_declarations, COUNT_OF(variable_declarations));
				printf("vars %d\n", num_variable_declarations);
				int numbits = 0;
				for (size_t i = 0; i < num_variable_declarations; ++i)
				{
					printf("var name = %s %s\n", AST_NODE_TYPE_to_string(variable_declarations[i]->type), variable_declarations[i]->variable_decl_data.id->identifier_data.name);
					
					int variable_size = data_type_size(ctx, variable_declarations[i]->variable_decl_data.data_type);
					numbits += variable_size;
				}
				
				printf("numbytes=%d\n",numbits/8);
				
				//process(ctx, n->func_decl_data.body);
				ctx->function = oldfunc;
			} else
			{
				perror("todo implement");
			}
		} break;
		
		case AST_VARIABLE_DECL:
		{
			struct ast_node *id = n->variable_decl_data.id;
			struct ast_node *data_type_node = n->variable_decl_data.data_type;
			struct ast_node *iv = n->variable_decl_data.initializer_value;
			assert(id->type == AST_IDENTIFIER);
			const char *variable_name = id->identifier_data.name;
			
			variable_t *tmp = hash_map_find(ctx->function->variables, variable_name);
			compiler_assert(ctx, !tmp, "variable already exists '%s'", variable_name);
			int variable_size = data_type_size(ctx, data_type_node);
			assert(variable_size > 0);
			if(ctx->function)
			{
				ctx->function->localvariablesize += variable_size;

				int offset = ctx->function->localvariablesize;
				
				variable_t tv = { .offset = offset, .is_param = 0, .data_type_node = data_type_node };
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
    if(setjmp(ctx->jmp))
    {
		return 1;
    }
	process(ctx, head);
	
	printf("functions:\n");
	hash_map_foreach_entry(ctx->functions, entry,
	{
		function_t *fn = entry->data;
		printf("%s\n", fn->name);
		printf("--------------------------\n");
		
		hash_map_foreach_entry(fn->variables, ventry,
		{
			printf("\t%s\n", ventry->key);
		});
	});
	
    return 0;
}
