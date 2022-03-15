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

static void vreg_map(compiler_t *ctx, vreg_t *out, vreg_t reg)
{
	if(reg == VREG_ANY)
	{
		reg = VREG_0;
		for(int i = 0; i < 4; ++i)
		{
			if(ctx->vregister_usage[VREG_0 + i] < ctx->vregister_usage[reg])
				reg = VREG_0 + i;
		}
	}
	if(ctx->vregister_usage[reg] > 0)
	{
		printf("push %s ; %d\n", vreg_names[reg], ctx->vregister_usage[reg]);
		ctx->cg.push(ctx, reg);
	}
	++ctx->vregister_usage[reg];
	*out = reg;
}

static void vreg_unmap(compiler_t *ctx, vreg_t *regptr)
{
	vreg_t reg = *regptr;
	
	if(ctx->vregister_usage[reg] > 1)
	{
		printf("pop %s ; %d\n", vreg_names[reg], ctx->vregister_usage[reg] - 1);
		ctx->cg.pop(ctx, reg);
		--ctx->vregister_usage[reg];
	}
}

void lvalue( compiler_t* ctx, struct ast_node* n, vreg_t reg )
{
	switch ( n->type )
	{
		case AST_IDENTIFIER:
		{
			variable_t* var = hash_map_find( ctx->function->variables, n->identifier_data.name );
			assert( var );
			ctx->cg.load_offset_from_stack_to_register(ctx, reg, var->offset, data_type_size(ctx, var->data_type_node) / 8);
		} break;
	}
}

int rvalue(compiler_t *ctx, ast_node_t *n, vreg_t reg)
{
	switch(n->type)
	{
		case AST_LITERAL:
			switch ( n->literal_data.type )
			{
			case LITERAL_INTEGER:
				printf("mov %s, %d\n", vreg_names[reg], n->literal_data.integer);
				ctx->cg.mov_r_imm32(ctx, reg, n->literal_data.integer, NULL);
				break;
			case LITERAL_STRING:
			{
				printf("mov %s, %s\n", vreg_names[reg], n->literal_data.string);
				ctx->cg.mov_r_string(ctx, reg, n->literal_data.string);
			} break;
			default:
				perror( "unhandled literal" );
				break;
			}
		break;
		
		case AST_BIN_EXPR:
		{
			struct ast_node *lhs = n->bin_expr_data.lhs;
			struct ast_node *rhs = n->bin_expr_data.rhs;
			
			vreg_t rhs_reg;
			vreg_map(ctx, &rhs_reg, VREG_ANY);
			rvalue(ctx, lhs, reg);
			rvalue(ctx, rhs, rhs_reg);

			switch(n->bin_expr_data.operator)
			{
				case '+':
				{
					vreg_t c = ctx->cg.add(ctx, reg, rhs_reg);
					printf("add %s, %s\n", vreg_names[reg], vreg_names[rhs_reg]);
					//ctx->cg.mov(ctx, reg, c);
				} break;
			}
			vreg_unmap(ctx, &rhs_reg);
		} break;

		case AST_ASSIGNMENT_EXPR:
		{
			
			vreg_t a, b;
			vreg_map(ctx, &a, VREG_ANY);
			vreg_map(ctx, &b, VREG_ANY);
			rvalue(ctx, n->assignment_expr_data.rhs, a);
			lvalue(ctx, n->assignment_expr_data.lhs, b);
			vreg_unmap(ctx, &a);
			vreg_unmap(ctx, &b);
			
			//TODO: FIXME
		} break;

		default:
			return 1;
	}
	return 0;
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

static struct ast_node *allocate_variable(compiler_t *ctx, struct ast_node *n, const char *varname, int offset, int size)
{
	variable_t tv = { .offset = offset, .is_param = 0, .data_type_node = n->variable_decl_data.data_type };
	hash_map_insert( ctx->function->variables, varname, tv );
}

vreg_t process(compiler_t* ctx, ast_node_t* n)
{
	codegen_t *cg = &ctx->cg;
	//printf("n->type=%s\n",AST_NODE_TYPE_to_string(n->type));
	switch (n->type)
    {
		case AST_PROGRAM:			
			linked_list_reversed_foreach( n->program_data.body, struct ast_node**, it, {
				process(ctx, (*it));
			} );
		break;

		case AST_RETURN_STMT:

		break;
		
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
					const char *variable_name = variable_declarations[i]->variable_decl_data.id->identifier_data.name;
					variable_t *tmp = hash_map_find(ctx->function->variables, variable_name);
					compiler_assert(ctx, !tmp, "variable already exists '%s'", variable_name);
					int variable_size = data_type_size(ctx, variable_declarations[i]->variable_decl_data.data_type);
					allocate_variable(ctx, variable_declarations[i], variable_name, numbits / 8, variable_size / 8);
					printf("var name = %s %s\n", AST_NODE_TYPE_to_string(variable_declarations[i]->type), variable_name);
					
					numbits += variable_size;
				}
				
				printf("numbytes=%d\n",numbits/8);
				
				process(ctx, n->func_decl_data.body);
				ctx->function = oldfunc;
			} else
			{
				perror("todo implement");
			}
		} break;
		
		case AST_VARIABLE_DECL:
		{
			struct ast_node *id = n->variable_decl_data.id;
			struct ast_node *iv = n->variable_decl_data.initializer_value;
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
		{
			vreg_t reg;
			vreg_map(ctx, &reg, VREG_ANY);
			if(rvalue(ctx, n, reg))
			{
				printf( "unhandled ast node type '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
				exit( -1 );
			}
			vreg_unmap(ctx, &reg);
			//printf("unhandled type %s | %s:%d\n", AST_NODE_TYPE_to_string(n->type), __FILE__, __LINE__);
		} break;
    }
}

static void print_hex(u8 *buf, size_t n)
{
	for (int i = 0; i < n; ++i)
	{
		printf("%02X%s", buf[i] & 0xff, i + 1 == n ? "" : " ");
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
		if(!strcmp(fn->name, "main"))
		{
			printf("bytecode=%d\n",heap_string_size(&fn->bytecode));
			print_hex(fn->bytecode, heap_string_size(&fn->bytecode));
		}
		printf("--------------------------\n");
		
		hash_map_foreach_entry(fn->variables, ventry,
		{
			printf("\t%s\n", ventry->key);
		});
	});
	
    return 0;
}
