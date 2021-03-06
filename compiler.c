#include <stdarg.h>
#include "ast.h"
#include "types.h"
#include "token.h"

#include "std.h"
#include "compile.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"
#include "codegen.h"

//TODO: FIXME this is not really "safe", but if just compile once then exit the process this is fine.
//if you have multiple instances of the compiler running simultaneously then you should fix this.
static codegen_t *cg = NULL;

struct ast_node *get_struct_member_info(compiler_t* ctx, struct ast_struct_decl *decl, const char *member_name, int *offset, int *size);

static void set8(compiler_t *ctx, int offset, u8 op)
{
    ctx->instr[offset] = op;
}

static void set32(compiler_t *ctx, int offset, u32 value)
{
    u32 *ptr = (u32*)&ctx->instr[offset];
    *ptr = value;
}

static void db(compiler_t *ctx, u8 op)
{
    heap_string_push(&ctx->instr, op);
}

static void dd(compiler_t *ctx, u32 i)
{
    union
    {
        uint32_t i;
        uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
		heap_string_push(&ctx->instr, u.b[i]);
}

static int instruction_position(compiler_t *ctx)
{
    return heap_string_size(&ctx->instr);
}

struct function* lookup_function_by_name(compiler_t *ctx, const char *name)
{    
    linked_list_reversed_foreach(ctx->functions, struct function*, it,
    {
        if(!strcmp(it->name, name))
            return it;
    });
    return NULL;
}

static int primitive_data_type_size(int type)
{
    switch(type)
	{
    case DT_CHAR: return 1;
    case DT_SHORT: return 2;
    case DT_INT: return 4;
    case DT_LONG: return 4;
    case DT_NUMBER: return 4;
    case DT_FLOAT: return 4;
    case DT_DOUBLE: return 4;
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
        return 4;
    case AST_PRIMITIVE:
        return primitive_data_type_size(n->primitive_data.primitive_type);
    case AST_ARRAY_DATA_TYPE:
	{
		assert( n->data_type_data.array_size > 0 );

		if ( n->data_type_data.data_type->type == AST_ARRAY_DATA_TYPE )
			return data_type_size( ctx, n->data_type_data.data_type ) * n->data_type_data.array_size;
		else if ( n->data_type_data.data_type->type == AST_PRIMITIVE )
		{
            //printf("array size = %d, primitive_type_size = %d\n", n->data_type_data.array_size, primitive_data_type_size(  n->data_type_data.data_type->primitive_data_type_data.primitive_type ));
			return primitive_data_type_size( n->data_type_data.data_type->primitive_data.primitive_type ) *
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

static int data_type_pass_by_reference(struct ast_node *n)
{
    if(n->type == AST_PRIMITIVE || n->type == AST_POINTER_DATA_TYPE)
        return 0;
    //for now pass everything else by reference, arrays etc
    return 1;
}

static int process(compiler_t *ctx, struct ast_node *n);

typedef enum
{
    FUNCTION_CALL_NOT_FOUND,
    FUNCTION_CALL_IMPORT_NOT_FOUND,
    FUNCTION_CALL_NORMAL,
    FUNCTION_CALL_IMPORT,
    FUNCTION_CALL_SYSCALL,
    FUNCTION_CALL_INT3
} FUNCTION_CALL_TYPE;

static FUNCTION_CALL_TYPE identify_function_call_type(compiler_t* ctx, const char* function_name, /*avoid looking up the symbols multiple times when we don't have to */struct function **fn_out, struct dynlib_sym** sym_out)
{
    if (!strcmp(function_name, "int3"))
        return FUNCTION_CALL_INT3;

    if (!strcmp(function_name, "syscall")
		&&
		(ctx->build_target == BT_OPCODES || ctx->build_target == BT_LINUX_X86 || ctx->build_target == BT_LINUX_X64)
		)
        return FUNCTION_CALL_SYSCALL;

    struct function *fn = lookup_function_by_name(ctx, function_name);
    if (fn)
    {
        *fn_out = fn;
        if (fn->location != -1)
            return FUNCTION_CALL_NORMAL;
        struct dynlib_sym* sym = ctx->find_import_fn(ctx->find_import_fn_userptr, function_name);
        if (sym)
        {
            *sym_out = sym;
            return FUNCTION_CALL_IMPORT;
        }
        return FUNCTION_CALL_IMPORT_NOT_FOUND;
    }

    return FUNCTION_CALL_NOT_FOUND;
}

static int function_call_ident(compiler_t *ctx, const char *function_name, struct ast_node **args, int numargs)
{
    struct function *fn;
    struct dynlib_sym* sym;
    int rvalue(compiler_t* ctx, reg_t reg, struct ast_node* n);

    FUNCTION_CALL_TYPE function_call_type = identify_function_call_type(ctx, function_name, &fn, &sym);
    switch (function_call_type)
    {
    default:
        printf("invalid function call, got result %d\n", function_call_type);
		cg->int3(ctx);
		cg->int3(ctx);
		cg->int3(ctx);
        return 1;
        break;

#if 0
    case FUNCTION_CALL_IMPORT:
    {
		//TODO: FIXME atm highly specialized for memory/win32, see memory.c also assumes that location is n + 6
        //TODO: FIXME make it work for building exe files aswell, e.g proper IAT thunk table
        for (int i = 0; i < numargs; ++i)
        {
            process(ctx, args[numargs - i - 1]);
			cg->push(ctx, EAX);
        }

		int from;
		int nbytes = cg->indirect_call_imm32(ctx, 0x0, &from);
		cg->jmp(ctx, nbytes);

        //location P
        //actual location of imported function
        dd(ctx, 0x0);

        if (numargs > 0)
        {
			//TODO: generalize
			cg->add_imm8_to_r32(ctx, ESP, numargs * 4);
        }

        struct relocation reloc = {
            .from = from,
            .to = (intptr_t)sym,
            .size = 4,
            .type = RELOC_IMPORT
        };
        linked_list_prepend(ctx->relocations, reloc);
    } break;
#endif

    case FUNCTION_CALL_INT3:
        //if (opt_flags & OPT_DEBUG)
			cg->int3(ctx);
        break;

    case FUNCTION_CALL_NORMAL:
    {
        for (int i = 0; i < numargs; ++i)
        {
            process(ctx, args[numargs - i - 1]);
			cg->push(ctx, EAX);
        }

		cg->call_imm32(ctx, fn->location);

        if (numargs > 0)
        {
			//TODO: FIXME numargs > 0xff
			cg->add_imm8_to_r32(ctx, ESP, numargs * 4);
        }
    } break;

    case FUNCTION_CALL_SYSCALL:
        cg->invoke_syscall(ctx, args, numargs);
        break;
    }
    return 0;
}

static int function_variable_declaration_stack_size( compiler_t* ctx, struct ast_node* n )
{
	assert( n->type == AST_FUNCTION_DECL );
	int nd = n->func_decl_data.numdeclarations;
	int total = 0;
	for ( int i = 0; i < nd; ++i )
	{
		struct ast_node* decl = n->func_decl_data.declarations[i];
		assert( decl->type == AST_VARIABLE_DECL );
		int ds = data_type_size( ctx, decl->variable_decl_data.data_type );
		assert( ds > 0 );
		total += ds;
	}
    
    //align to 32
    //TODO: fix this make sure the esp value is aligned instead
    int aligned = total & ~31;
    if(aligned == 0)
        return 32;
    return aligned;
}

#if 0 
static int accumulate_local_variable_declaration_size(compiler_t *ctx, struct ast_node *n)
{
    assert(n->type == AST_BLOCK_STMT);
    
    int total = 0;
    //TODO: fix this and make it change depending on variable type declaration instead of assignment
    linked_list_reversed_foreach(n->block_stmt_data.body, struct ast_node**, it,
    {
        if((*it)->type == AST_VARIABLE_DECL)
		{
            int ds = data_type_size((*it)->variable_decl_data.data_type);
            assert(ds > 0);
            total += ds;
		}
#if 0
        if((*it)->type == AST_ASSIGNMENT_EXPR && (*it)->assignment_expr_data.operator == '=')
        {
            total += 4; //FIXME: shouldn't always be 4 bytes
        }
#endif
    });
    return total;
    /*
    //align to 32
    //TODO: fix this make sure the esp value is aligned instead
    int aligned = total & ~31;
    if(aligned == 0)
        return 32;
    return aligned;
    */
}
#endif

static intptr_t register_value(compiler_t *ctx, reg_t reg)
{
    return ctx->registers[reg];
}

static struct ast_node *identifier_data_node(compiler_t *ctx, struct ast_node *n)
{
    if(n->type != AST_IDENTIFIER)
        debug_printf("expected identifier, got '%s'\n", AST_NODE_TYPE_to_string(n->type));
	assert(n->type == AST_IDENTIFIER);
    struct variable *var = hash_map_find(ctx->function->variables, n->identifier_data.name);
    assert(var);
    return var->data_type_node;
}

static int data_type_operand_size(compiler_t *ctx, struct ast_node *n, int ptr)
{
	switch ( n->type )
	{
	// not a datatype, but we'll resolve it
	case AST_IDENTIFIER:
        return data_type_operand_size(ctx, identifier_data_node(ctx, n), ptr);

	case AST_PRIMITIVE:
		return primitive_data_type_size( n->primitive_data.primitive_type );
        
#ifndef MIN
#define MIN( a, b ) ( ( a ) > ( b ) ? ( b ) : ( a ) )
#endif
	case AST_BIN_EXPR:
        return MIN( data_type_operand_size(ctx, n->bin_expr_data.lhs, ptr), data_type_operand_size(ctx, n->bin_expr_data.rhs, ptr) );

    case AST_LITERAL:
        return 4;

	case AST_POINTER_DATA_TYPE:
		if ( ptr )
			return 4;
		if ( n->data_type_data.data_type->type == AST_POINTER_DATA_TYPE )
			return data_type_operand_size( ctx, n->data_type_data.data_type, 1 );
		else
			return primitive_data_type_size(
				n->data_type_data.data_type->primitive_data.primitive_type );

	case AST_ARRAY_DATA_TYPE:
		return primitive_data_type_size( n->data_type_data.data_type->primitive_data.primitive_type );

    case AST_STRUCT_MEMBER_EXPR:
    {
		struct ast_node* idn = identifier_data_node(ctx, n->member_expr_data.object);
        //ast_print_node_type("idn", idn);
		assert(idn->type == AST_STRUCT_DATA_TYPE || idn->type == AST_POINTER_DATA_TYPE);
        struct ast_node *sr = NULL;
        if(idn->type == AST_POINTER_DATA_TYPE)
            sr = idn->data_type_data.data_type->data_type_data.data_type;
        else
            sr = idn->data_type_data.data_type;
            
		for (int i = 0; i < sr->struct_decl_data.numfields; ++i)
		{
			if (!strcmp(sr->struct_decl_data.fields[i]
							->variable_decl_data.id->identifier_data.name,
						n->member_expr_data.property->identifier_data.name))
			{
				return data_type_operand_size(
					ctx,
					sr->struct_decl_data.fields[i]->variable_decl_data.data_type,
					ptr);
			}
		}
		abort();
		return 0;
	} break;
        
    case AST_MEMBER_EXPR:
    {
		assert(n->member_expr_data.object->type == AST_IDENTIFIER);
		return data_type_operand_size(ctx, n->member_expr_data.object, 0);
	} break;
	case AST_WHILE_STMT:
        return data_type_operand_size(ctx, n->while_stmt_data.test, ptr);
    case AST_DO_WHILE_STMT:
        return data_type_operand_size(ctx, n->do_while_stmt_data.test, ptr);
    case AST_FOR_STMT:
        return data_type_operand_size(ctx, n->for_stmt_data.test, ptr);
    case AST_UNARY_EXPR:
		if ( n->unary_expr_data.operator== '*' )
			return data_type_operand_size( ctx, n->unary_expr_data.argument, 0 );
		return data_type_operand_size(ctx, n->unary_expr_data.argument, ptr);
    case AST_CAST:
        return data_type_operand_size(ctx, n->cast_data.type, ptr);
	case AST_DATA_TYPE:
		return data_type_operand_size(ctx, n->data_type_data.data_type, ptr);
	}
	debug_printf( "unhandled data type '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
	return 0;
}

int rvalue(compiler_t *ctx, reg_t reg, struct ast_node *n);
int lvalue(compiler_t *ctx, reg_t reg, struct ast_node *n);
void store_operand(compiler_t *ctx, struct ast_node *n);
static void ast_handle_assignment_expression( compiler_t* ctx, struct ast_node* n )
{
    cg->push(ctx, EBX);
	struct ast_node* lhs = n->assignment_expr_data.lhs;
	struct ast_node* rhs = n->assignment_expr_data.rhs;

	rvalue( ctx, EAX, rhs );
	// we should now have our result in eax

	switch ( n->assignment_expr_data.operator)
	{
	case TK_PLUS_ASSIGN:
	{
		cg->push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		// load_variable(ctx, EBX, 1, lhs, 0);
		cg->pop( ctx, EAX );
		
		cg->load_reg(ctx, EAX, EBX);
		cg->add(ctx, EBX, EAX);
		cg->store_reg(ctx, EBX, EAX);
	}
	break;
	case TK_MINUS_ASSIGN:
	{
		cg->push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		// load_variable(ctx, EBX, 1, lhs, 0);
		cg->pop( ctx, EAX );
		
		cg->load_reg(ctx, EAX, EBX);
		cg->sub(ctx, EBX, EAX);
		cg->store_reg(ctx, EBX, EAX);
	}
	break;
		// TODO: add mul and other operators

	case TK_MOD_ASSIGN:
	{
		cg->mov(ctx, ESI, EAX);
		// TODO: maybe push esi, incase we trash the register
		rvalue( ctx, EAX, lhs );
		cg->xor(ctx, EDX, EDX);
		
		cg->idiv(ctx, ESI);

		cg->push( ctx, EDX );
		lvalue( ctx, EBX, lhs );
		cg->pop( ctx, EDX );

		cg->store_reg(ctx, EBX, EDX);
	}
	break;

	case TK_DIVIDE_ASSIGN:
	{
		cg->mov(ctx, ESI, EAX);

		// TODO: maybe push esi, incase we trash the register
		rvalue( ctx, EAX, lhs );

		cg->xor(ctx, EDX, EDX);

		cg->idiv(ctx, ESI);

		cg->push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		cg->pop( ctx, EAX );
		
		cg->store_reg(ctx, EBX, EAX);
	}
	break;

	case TK_MULTIPLY_ASSIGN:
	{
		cg->mov(ctx, ESI, EAX);

		// TODO: maybe push esi, incase we trash the register
		rvalue( ctx, EAX, lhs );
		
		cg->xor(ctx, EDX, EDX);

		cg->imul(ctx, ESI);

		cg->push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		cg->pop( ctx, EAX );
		
		cg->store_reg(ctx, EBX, EAX);
	}
	break;

	case '=':
	{
		int os = data_type_operand_size( ctx, lhs, 1 );
		cg->push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		cg->pop( ctx, EAX );
		store_operand( ctx, lhs );
	}
	break;

	default:
		printf( "unhandled assignment operator\n" );
		break;
	}
    cg->pop(ctx, EBX);
}


void store_operand(compiler_t *ctx, struct ast_node *n)
{
	int os = data_type_operand_size( ctx, n, 1 );
	// TODO: fix hardcoded EBX
	switch ( os )
	{
	case 4:
		cg->store_reg(ctx, EBX, EAX);
		break;
    case 2:
		// mov word ptr [ebx], ax
		db( ctx, 0x66 );
		db( ctx, 0x89 );
		db( ctx, 0x03 );
        break;
	case 1:
		// mov byte ptr [ebx], al
		db( ctx, 0x88 );
		db( ctx, 0x03 );
		break;
	default:
		//TODO: FIXME throw a proper error
		//ran into issue with
		//void *buffer = malloc(n);
		//where buffer[offset] = 0;
		//resulted in a error with operand size of 0
		//because void operand size is 0
		//and the above doesn't make sense
		//preferably with a node token / source line file debug information
		debug_printf( "unhandled operand size %d for node '%s'\n", os, AST_NODE_TYPE_to_string(n->type));
		exit( 1 );
		break;
	}
}

static int load_operand(compiler_t *ctx, struct ast_node *n)
{
	int os = data_type_operand_size( ctx, n, 0 );
	switch ( os )
	{
	case 4:
		cg->load_reg(ctx, EAX, EBX);
		break;
    case 2:
		// movzx eax, word [ebx]
		db( ctx, 0x0f );
		db( ctx, 0xb7 );
		db( ctx, 0x03 );
        break;
	case 1:
		// movzx eax, byte [ebx]
		db( ctx, 0x0f );
		db( ctx, 0xb6 );
		db( ctx, 0x03 );
		break;
	default:
		debug_printf( "unhandled regsz '%d' for load_operand \n", os );
		exit( 1 );
		break;
	}
    return os;
}

int rvalue(compiler_t *ctx, reg_t reg, struct ast_node *n)
{
    //printf("rvalue node '%s'\n", AST_NODE_TYPE_to_string(n->type));
    switch(n->type)
	{
	case AST_LITERAL:
		switch ( n->literal_data.type )
		{
		case LITERAL_INTEGER:
            cg->mov_r_imm32(ctx, reg, n->literal_data.integer, NULL);
			break;
		case LITERAL_STRING:
		{
            cg->mov_r_string(ctx, reg, n->literal_data.string);
		}
		break;
		default:
			perror( "unhandled literal" );
			break;
		}
		break;

	case AST_IDENTIFIER:
	{
		// TODO: remove this and move to lvalue, then rvalue will call lvalue then load the identifier into EAX
		const char* variable_name = n->identifier_data.name;
		struct variable* var = hash_map_find( ctx->function->variables, variable_name );
        if(!var)
            printf("var '%s' does not exist\n", variable_name);
		assert( var );
        int offset = var->is_param ? 4 + var->offset : 0xff - var->offset + 1;
        switch(var->data_type_node->type)
		{
        case AST_ARRAY_DATA_TYPE:
			// lea r32,[ebp - offset]
			//db(ctx, 0x8d);
			//db(ctx, 0x85 + 8 * reg);
			//dd(ctx, offset);
			//basically same as lea, except with more steps
			cg->mov(ctx, reg, EBP);
			cg->sub_regn_imm32(ctx, reg, offset);
			break;
            
        default:
		{
			int nbytes = data_type_operand_size( ctx, n, 1 );
            switch(nbytes)
			{
            case 4:
				cg->load_regn_base_offset_imm32(ctx, reg, offset);
				break;
            case 2:
            case 1:
                cg->push(ctx,EBX);
                lvalue(ctx,EBX,n);
                load_operand(ctx, n);
                cg->pop(ctx,EBX);
                break;
            default:
                perror("unhandled case for loading identifier rvalue");
                break;
			}
		}
		break;
		}
	}
	break;

	case AST_TERNARY_EXPR:
	{
        struct ast_node *consequent = n->ternary_expr_data.consequent;
        struct ast_node *condition = n->ternary_expr_data.condition;
        struct ast_node *alternative = n->ternary_expr_data.alternative;

        process(ctx, condition);
		cg->test(ctx, EAX, EAX);

		// jz rel32
		int jz_pos = instruction_position( ctx ); // jmp_pos + 2 = new_pos
		db( ctx, 0x0f );
		db( ctx, 0x84 );
		dd( ctx, 0x0 ); // placeholder
        
        process(ctx, consequent);
        
		int jmp_pos = instruction_position( ctx );
		db( ctx, 0xe9 );
		dd( ctx, 0x0 ); // placeholder
        set32( ctx, jz_pos + 2, instruction_position( ctx ) - jz_pos - 6 );
        process(ctx, alternative);
        set32( ctx, jmp_pos + 1, instruction_position( ctx ) - jmp_pos - 5 );
	} break;

	case AST_BIN_EXPR:
    {
        struct ast_node *lhs = n->bin_expr_data.lhs;
        struct ast_node *rhs = n->bin_expr_data.rhs;

        rvalue(ctx, EAX, lhs);
        
        cg->push( ctx, EAX );
		rvalue( ctx, EAX, rhs );
		cg->mov(ctx, ECX, EAX);
        
		cg->pop( ctx, EAX );

		cg->xor(ctx, EDX, EDX);

		reljmp_t rel;
        switch(n->bin_expr_data.operator)
        {
        case '*':
			cg->imul(ctx, ECX);
            break;
        case '/':
			cg->idiv(ctx, ECX);
            break;

        case '+':
			cg->add(ctx, EAX, ECX);
            break;
        case '-':
			cg->sub(ctx, EAX, ECX);
            break;
        case '&':
			cg->and(ctx, EAX, ECX);
            break;
        case '|':
			cg->or(ctx, EAX, ECX);
            break;
        case '^':
			cg->xor(ctx, EAX, ECX);
            break;
        case TK_LSHIFT:
            db(ctx, 0xd3);
            db(ctx, 0xf0);
            break;
        case TK_RSHIFT:
            db(ctx, 0xd3);
            db(ctx, 0xf8);
            break;
        case '%':
			//idiv(ctx, ECX);
			//mov(ctx, EAX, EDX);
			//TODO: FIXME other architectures won't neccessarily put the remainder into EDX
			
			//do this for other cases aswell and TODO generalize more e.g mov(ctx, REG0, add(REG1, REG2))
			//mod returns the register that the remainder is placed into
			//then when doing mov with same register, don't emit any extra opcodes
			
			//for now just pass the default registers that x86 expects
			//TODO: FIXME what if the result is seperated over 2 registers e.g EAX:EDX, register structure with flags which one is enabled? for now just use 1
			//mov(ctx, EAX, mod(ctx, EAX, ECX));
			cg->mod(ctx, EAX, ECX);
            break;

        case TK_LEQUAL:
			cg->cmp(ctx, EAX, ECX);
			cg->xor(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JG);
				cg->inc(ctx, EAX);
			cg->jmp_end(ctx, &rel);
		break;
		
        case '>':
			cg->cmp(ctx, EAX, ECX);
			cg->xor(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JLE);
				cg->inc(ctx, EAX);
			cg->jmp_end(ctx, &rel);
			break;
        case '<':
			cg->cmp(ctx, EAX, ECX);
			cg->xor(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JGE);
				cg->inc(ctx, EAX);
			cg->jmp_end(ctx, &rel);
		break;
        case TK_EQUAL:
			cg->cmp(ctx, EAX, ECX);
			cg->xor(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JNE);
				cg->inc(ctx, EAX);
			cg->jmp_end(ctx, &rel);
			break;
        case TK_NOT_EQUAL:
			cg->cmp(ctx, EAX, ECX);
			cg->xor(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JE);
				cg->inc(ctx, EAX);
			cg->jmp_end(ctx, &rel);
			break;
        case TK_GEQUAL:
			cg->cmp(ctx, EAX, ECX);
			cg->xor(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JL);
				cg->inc(ctx, EAX);
			cg->jmp_end(ctx, &rel);
			break;

        default:
            printf("unhandled operator (%d) %c\n", n->bin_expr_data.operator, n->bin_expr_data.operator);
            break;
        }
    } break;

    case AST_ASSIGNMENT_EXPR:
	{
        ast_handle_assignment_expression(ctx, n);
	} break;

	case AST_SIZEOF:
	{
        int sz = 0;
        switch(n->sizeof_data.subject->type)
		{
        case AST_PRIMITIVE:
        case AST_POINTER_DATA_TYPE:
        case AST_ARRAY_DATA_TYPE:
        case AST_STRUCT_DATA_TYPE:
			sz = data_type_size( ctx, n->sizeof_data.subject );
			break;
		case AST_DATA_TYPE:
			sz = data_type_size(ctx,n->data_type_data.data_type);
			break;
		case AST_IDENTIFIER:
		{
            struct variable *var = hash_map_find(ctx->function->variables, n->sizeof_data.subject->identifier_data.name);
			assert( var );
			sz = data_type_size( ctx, var->data_type_node );
		}
		break;
		default:
            debug_printf("unhandled sizeof '%s'\n", AST_NODE_TYPE_to_string(n->sizeof_data.subject->type));
            break;
		}
        assert(sz>0);
		cg->mov_r_imm32(ctx, EAX, sz, NULL);
	} break;

    case AST_STRUCT_MEMBER_EXPR:
    {
        cg->push(ctx, EBX);
        struct ast_node *object = n->member_expr_data.object;
		struct ast_node *dn = identifier_data_node(ctx, object);
        //ast_print_node_type("object", dn);
		if ( dn->type == AST_POINTER_DATA_TYPE )
        {
			rvalue( ctx, EAX, object );
			cg->mov(ctx, EBX, EAX);
		}
		else
			lvalue( ctx, EBX, object );
        
		struct ast_node* sr = NULL;
		if (dn->type == AST_POINTER_DATA_TYPE)
			sr = dn->data_type_data.data_type->data_type_data.data_type;
		else
			sr = dn->data_type_data.data_type;

		assert(n->member_expr_data.property->type == AST_IDENTIFIER);
		int off, sz;
		struct ast_node *field = get_struct_member_info(ctx, &sr->struct_decl_data, n->member_expr_data.property->identifier_data.name, &off,
							   &sz);
		assert(sz > 0);
		
		cg->add_imm32_to_r32(ctx, EBX, off);

		load_operand(ctx, field->variable_decl_data.data_type);
        cg->pop(ctx, EBX);
	} break;    

    case AST_MEMBER_EXPR:
    {
        cg->push(ctx, EBX);
        struct ast_node *object = n->member_expr_data.object;
        struct ast_node *property = n->member_expr_data.property;
		struct ast_node *dn = identifier_data_node(ctx, object);
        //ast_print_node_type("object", dn);
		if ( dn->type == AST_POINTER_DATA_TYPE )
        {
			rvalue( ctx, EAX, object );
			
			cg->mov(ctx, EBX, EAX);
		}
		else
			lvalue( ctx, EBX, object );
        
        rvalue( ctx, reg, n->member_expr_data.property );
        
		int os = data_type_operand_size( ctx, object, 0 );

		cg->push( ctx, ESI );
		cg->mov_r_imm32(ctx, ESI, os, NULL);
		
		cg->imul(ctx, ESI);
        
		cg->pop( ctx, ESI );
        
        cg->add( ctx, EBX, reg );
        load_operand(ctx, dn);
        cg->pop(ctx, EBX);
	} break;
    
    case AST_UNARY_EXPR:
    {
        struct ast_node *arg = n->unary_expr_data.argument;
        if(arg->type == AST_LITERAL)
        {
            switch(n->unary_expr_data.operator)
            {
            case '-':
				cg->mov_r_imm32(ctx, EAX, -arg->literal_data.integer, NULL);
                break;
                
            case '+':
				cg->mov_r_imm32(ctx, EAX, arg->literal_data.integer, NULL);
                break;

            case '!':
				cg->mov_r_imm32(ctx, EAX, !arg->literal_data.integer, NULL);
                break;
                
            case '~':
				cg->mov_r_imm32(ctx, EAX, ~arg->literal_data.integer, NULL);
                break;

			default:
                printf("unhandled unary expression %c\n", n->unary_expr_data.operator);
                break;
            }
        } else
        {
            switch(n->unary_expr_data.operator)
			{
			case '*':
			{
				// TODO: FIXME hack, checking whether next node is ++, to prevent using tmp memory somewhere or use
				// the lea edx, [eax + increment] trick
				struct ast_node* arg = n->unary_expr_data.argument;
				if ( arg->type == AST_UNARY_EXPR )
				{

					switch ( arg->unary_expr_data.operator)
					{
					case TK_PLUS_PLUS:
					{
						struct ast_node* pparg = arg->unary_expr_data.argument;
						assert(!arg->unary_expr_data.prefix);
						cg->push( ctx, EBX );
						// let's assume pparg is a identifier of type const char *
						lvalue( ctx, EBX, pparg );
                        
                        //save our ptr to our variable to edx
						cg->push(ctx, EDX);
						cg->mov(ctx, EDX, EBX);

                        //load the actual char * pointer e.g "test" into ebx
						cg->load_reg(ctx, EBX, EBX);

                        //then load our value from ebx (can be a char, int or any type, in this case char 8 bits)
						int os = load_operand( ctx, pparg ); //loads location in EBX into EAX register
                        //printf("os = %d\n", os);
                        
                        //increment the actual pointer value of our string
                        
						//basically inc [edx]
						cg->push(ctx, EAX);
						cg->load_reg(ctx, EAX, EDX);
						cg->inc(ctx, EAX);
						cg->store_reg(ctx, EDX, EAX);
						cg->pop(ctx, EAX);

                        //restore our EDX value if we ever used it for anything else.
                        cg->pop(ctx, EDX);
                        
						cg->pop( ctx, EBX );
					}
					break;

					default:
						debug_printf( "unhandled operator after dereference\n" );
						return 1;
					}
				}
				else
				{
					cg->push( ctx, EBX );
					rvalue( ctx, reg, n->unary_expr_data.argument );
					cg->mov(ctx, EBX, EAX);
					load_operand( ctx, n->unary_expr_data.argument );
					cg->pop( ctx, EBX );
				}
			} break;

            case TK_PLUS_PLUS:
				// handle generic ++ case
				cg->push( ctx, EBX );
				lvalue( ctx, EBX, n->unary_expr_data.argument );
				if(!n->unary_expr_data.prefix)
				{
					load_operand( ctx, n->unary_expr_data.argument );
					cg->push(ctx, EAX);
					cg->load_reg(ctx, EAX, EBX);
					cg->inc(ctx, EAX);
					cg->store_reg(ctx, EBX, EAX);
					cg->pop(ctx, EAX);
				} else
				{
					cg->push(ctx, EAX);
					cg->load_reg(ctx, EAX, EBX);
					cg->inc(ctx, EAX);
					cg->store_reg(ctx, EBX, EAX);
					cg->pop(ctx, EAX);
					load_operand( ctx, n->unary_expr_data.argument );
				}
				cg->pop( ctx, EBX );
				break;
			case '&':
                cg->push(ctx, EBX);
				lvalue( ctx, EBX, n->unary_expr_data.argument );
				if ( reg == EAX )
				{
					cg->mov(ctx, EAX, EBX);
				}
				cg->pop( ctx, EBX );
				break;
                
			case '-':
                rvalue(ctx, EAX, arg);
				cg->neg(ctx, EAX);
                break;
            case '!':
            case '~':
				perror("unhandled");
				/*
                rvalue(ctx, EAX, arg);
                if(n->unary_expr_data.operator=='!')
                {
                    //cmp eax, 0
                    db(ctx, 0x83);
                    db(ctx, 0xf8);
                    db(ctx, 0x00);
                    
                    //sete al
                    db(ctx, 0x0f);
                    db(ctx, 0x94);
                    db(ctx, 0xc0);

                    //movzx eax, al
                    db(ctx, 0x0f);
                    db(ctx, 0xb6);
                    db(ctx, 0xc0);
                } else
				{
					// not eax
					db( ctx, 0xf7 );
					db( ctx, 0xd0 );
				}
				*/
				break;

			default:
				cg->push( ctx, EBX );
				if ( lvalue( ctx, EBX, n ) )
				{
					debug_printf( "unhandled unary expression '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
					abort();
				}
                load_operand(ctx, n);
				cg->pop( ctx, EBX );
				break;
			}
		}
    } break;

	case AST_FUNCTION_CALL_EXPR:
	{
		struct ast_node** args = n->call_expr_data.arguments;
		int numargs = n->call_expr_data.numargs;
		struct ast_node* callee = n->call_expr_data.callee;

		if ( callee->type == AST_IDENTIFIER )
		{
			int ret = function_call_ident( ctx, callee->identifier_data.name, args, numargs );
			if ( ret )
			{
				FIXME( "cannot find function '%s'\n", callee->identifier_data.name );
			}
		}
		else
		{
			FIXME( "unhandled function call expression callee type" );
		}
	}
	break;
    
    case AST_SEQ_EXPR:
	{
        struct ast_node *expr = n->seq_expr_data.expr[0];
        rvalue(ctx, reg, expr);
        cg->push(ctx, EAX);
        for(int i = 1; i < n->seq_expr_data.numexpr; ++i)
		{
            rvalue(ctx, reg, n->seq_expr_data.expr[i]);
		}
		cg->pop(ctx, EAX);
	} break;

	default:
        cg->push(ctx, EBX);
        if(lvalue(ctx, EBX, n))
		{
			debug_printf( "unhandled rvalue '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
			exit( -1 );
		}
        load_operand(ctx, n);
        cg->pop(ctx, EBX);
		return 1;
	}
    return 0;
}

struct ast_node *get_struct_member_info(compiler_t* ctx, struct ast_struct_decl *decl, const char *member_name, int *offset, int *size)
{
    int total_offset = 0;
    for(int i = 0; i < decl->numfields; ++i)
	{
        int sz = data_type_operand_size(ctx, decl->fields[i]->variable_decl_data.data_type, 1);
		if (!strcmp(decl->fields[i]->variable_decl_data.id->identifier_data.name, member_name))
		{
            *offset = total_offset;
            *size = sz;
            return decl->fields[i];
		}
        total_offset += sz;
	}
    return NULL;
}

// locator value, can be local variable, global variable, array offset or any other valid lvalue
int lvalue( compiler_t* ctx, reg_t reg, struct ast_node* n )
{
	switch ( n->type )
	{
	case AST_IDENTIFIER:
	{
		const char* variable_name = n->identifier_data.name;
		struct variable* var = hash_map_find( ctx->function->variables, variable_name );
		assert( var );
        struct ast_node *variable_type = var->data_type_node;
        int offset = var->is_param ? 4 + var->offset : 0xff - var->offset + 1;

		cg->mov(ctx, reg, EBP);
		cg->sub_regn_imm32(ctx, reg, offset);
	} break;
    
	case AST_STRUCT_MEMBER_EXPR:
	{
		struct ast_node* object = n->member_expr_data.object;
		if(object->type == AST_STRUCT_MEMBER_EXPR)
		{
			//TODO: FIXME add support for nested struct member expressions
			/*
			// ast example
			assignment expression operator =
					struct member expression .
							struct member expression .
									identifier 'a'
									identifier 'v'
							identifier 'x'
			*/
			/*
				//NOTE when fixing this, also make sure to load the location that a pointer is pointing to instead of the value of the location.
				//basically do the same down below with if check pointer then either use lvalue or rvalue
				a.y

				struct a {int x,y,z;};

				a = loc 100
				y = offset 4

				100 + 4

				struct b { char z[32]; struct a; };
				b = loc 300
				b.a = offset 32

				b.a.y

				300 + 32
				+ 4
			*/

		}
		struct ast_node* dn = identifier_data_node(ctx, object);
        switch(dn->type)
		{
		case AST_POINTER_DATA_TYPE:
		case AST_STRUCT_DATA_TYPE:
		{
			struct ast_node* sr = NULL;
			if (dn->type == AST_POINTER_DATA_TYPE)
				sr = dn->data_type_data.data_type->data_type_data.data_type;
			else
				sr = dn->data_type_data.data_type;

			assert(n->member_expr_data.property->type == AST_IDENTIFIER);
			int off, sz;
			struct ast_node *field = get_struct_member_info(ctx, &sr->struct_decl_data, n->member_expr_data.property->identifier_data.name, &off,
								   &sz);
			assert(field);
			//TODO: FIXME nested union/struct types
			//for now the offset for any field in the struct is always 0
			//altough if we then reference a struct that may be added and the union will just always be 0
			//e.g union.struct_field.b
			//union.struct_field = 0
			//struct_field.b can be non-zero
			if(sr->type == AST_UNION_DECL)
			{
				off = 0;
			}
			assert(sz > 0);
			// printf("offset = %d, sz = %d for '%s'\n", off, sz, n->member_expr_data.property->identifier_data.name);

			cg->push(ctx, EAX);
			if (dn->type == AST_POINTER_DATA_TYPE)
			{
                //should be good to go for identifiers, most of the rvalue values are still hardcoded to EAX though...
                assert(object->type == AST_IDENTIFIER);
				rvalue(ctx, reg, object);
			}
			else
				lvalue(ctx, reg, object);
			
			cg->add_imm32_to_r32(ctx, EBX, off);

			cg->pop(ctx, EAX);
		}
		break;
		}
	}
	break;

	case AST_MEMBER_EXPR:
	{
		// TODO: FIXME for multidimensional arrays
		struct ast_node* object = n->member_expr_data.object;
		struct ast_node* dn = identifier_data_node( ctx, object );

        switch(dn->type)
		{
		case AST_ARRAY_DATA_TYPE:
		case AST_POINTER_DATA_TYPE: //only when it's a pointer to an primtive array type e.g const char * or int *
			cg->push(ctx, EAX);
			if (dn->type == AST_POINTER_DATA_TYPE)
			{
                //should be good to go for identifiers, most of the rvalue values are still hardcoded to EAX though...
                assert(object->type == AST_IDENTIFIER);
				rvalue(ctx, reg, object);
			}
			else
				lvalue(ctx, reg, object);
			rvalue(ctx, EAX, n->member_expr_data.property);

			int os = data_type_operand_size(ctx, object, 0);

			cg->push(ctx, ESI);
			cg->mov(ctx, ESI, os);
			cg->imul(ctx, ESI);
			cg->pop(ctx, ESI);

			cg->add(ctx, EBX, EAX);
			cg->pop(ctx, EAX);

			break;
        default:
            debug_printf("unhandled member expr case '%s'\n", AST_NODE_TYPE_to_string(dn->type));
            abort();
            break;
		}

	} break;

	case AST_UNARY_EXPR:
	{
		//printf( "operator = %s\n", token_type_to_string( n->unary_expr_data.operator) );
		switch ( n->unary_expr_data.operator)
		{
		case '*':
			lvalue( ctx, reg, n->unary_expr_data.argument );
			cg->load_reg(ctx, EBX, EBX);
			break;

		default:
			debug_printf( "unhandled unary lvalue '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
			return 1;
		}
	}
	break;
    
	case AST_CAST:
	{
        lvalue(ctx, reg, n->cast_data.expr);
        struct ast_node *from_type = n->cast_data.expr;
        struct ast_node *to_type = n->cast_data.type;
        switch(from_type->type)
		{
        case AST_IDENTIFIER:
		{
            struct ast_node *ident_node = identifier_data_node(ctx, n->cast_data.expr);
            switch(ident_node->type)
			{
			default:
                //pointers of different sizes will be handled in e.g data_type_size
                if(ident_node->type != to_type->type)
				{
					printf( "lvalue: cannot cast identifier type '%s' to '%s'\n",
							AST_NODE_TYPE_to_string( ident_node->type ), AST_NODE_TYPE_to_string( to_type->type ) );
					exit( -1 );
				}
				break;
			}
		} break;

		default:
            if(from_type->type != to_type->type)
			{
				printf( "lvalue: cannot cast '%s' to '%s'\n", AST_NODE_TYPE_to_string( from_type->type ),
						AST_NODE_TYPE_to_string( to_type->type ) );
				exit( -1 );
			}
			break;
		}
	} break;

	default:
        debug_printf("unhandled lvalue '%s'\n", AST_NODE_TYPE_to_string(n->type));
        return 1;
	}
    return 0;
}

static struct scope *active_scope(compiler_t *ctx)
{
    if(ctx->scope_index == 0)
        return NULL;
    return ctx->scope[ctx->scope_index - 1];
}

static void enter_scope(compiler_t *ctx, struct scope *scope)
{
    assert(ctx->scope_index + 1 < COUNT_OF(ctx->scope));
    scope->numbreaks = 0;
    ctx->scope[ctx->scope_index++] = scope;
}

static void exit_scope(compiler_t *ctx)
{
    --ctx->scope_index;
    ctx->scope[ctx->scope_index] = NULL;
}

static int process(compiler_t *ctx, struct ast_node *n)
{
    switch(n->type)
    {
    case AST_EMIT:
    {
		perror("emit is removed");
    } break;

    case AST_STRUCT_DECL:
    {
        printf("struct data type!\n");
        for(int i = 0; i < n->struct_decl_data.numfields; ++i)
		{
			printf("%d: %s\n", i, n->struct_decl_data.fields[i]->variable_decl_data.id->identifier_data.name);
		}
	} break;
        
    case AST_IF_STMT:
    {
        struct ast_node *consequent = n->if_stmt_data.consequent;
        struct ast_node *condition = n->if_stmt_data.test;
        struct ast_node *alternative = n->if_stmt_data.alternative;

        rvalue(ctx, EAX, condition);
		cg->test(ctx, EAX, EAX);
		reljmp_t rel;
		
		cg->jmp_begin(ctx, &rel, RJ_JZ);
			process(ctx, consequent);
		cg->jmp_end(ctx, &rel);

		//TODO: FIXME not optimized
		if ( alternative )
		{
			cg->jmp_begin(ctx, &rel, RJ_JNZ);
				process(ctx, alternative);
			cg->jmp_end(ctx, &rel);
		}
	}
	break;

	case AST_RETURN_STMT:
    {
        struct ast_node *expr = n->return_stmt_data.argument;
        process(ctx, expr);
        
		cg->mov(ctx, ESP, EBP);
		cg->pop(ctx, EBP);
		cg->ret(ctx);
    } break;
    
    //TODO: implement this properly
    case AST_FUNCTION_DECL:
    {
        int loc = instruction_position(ctx);
        if (n->func_decl_data.body) //no body means just a empty declaration, so ignore creating opcodes for it
        {
            if (!strcmp(n->func_decl_data.id->identifier_data.name, "main"))
            {
                //printf("set entry call to 0x%02X (%d)\n", instruction_position( ctx ), instruction_position( ctx ));
                ctx->entry = instruction_position(ctx);
            }
            struct function func = {
                .location = loc,
                .name = n->func_decl_data.id->identifier_data.name,
                .localvariablesize = 0,
                .variables = hash_map_create(struct variable)
            };
            ctx->function = linked_list_prepend(ctx->functions, func);
            int offset = 0;
            for (int i = 0; i < n->func_decl_data.numparms; ++i)
            {
                struct ast_node* parm = n->func_decl_data.parameters[i];
                assert(parm->type == AST_VARIABLE_DECL);

                offset += data_type_size(ctx, parm->variable_decl_data.data_type);

                struct variable tv = {
                        .offset = offset,
                        .is_param = 1,
                        .data_type_node = parm->variable_decl_data.data_type
                };

                assert(parm->variable_decl_data.id->type == AST_IDENTIFIER);
                hash_map_insert(ctx->function->variables, parm->variable_decl_data.id->identifier_data.name, tv);
            }
            assert(n->func_decl_data.body->type == AST_BLOCK_STMT);
            //int localsize = accumulate_local_variable_declaration_size(ctx, n->func_decl_data.body);
            int localsize = function_variable_declaration_stack_size(ctx, n);
			cg->push(ctx, EBP);
			cg->mov(ctx, EBP, ESP);

			cg->sub_regn_imm32(ctx, ESP, localsize);

            process(ctx, n->func_decl_data.body);
			
			cg->mov(ctx, ESP, EBP);
			cg->pop(ctx, EBP);
			cg->ret(ctx);
        }
        else
        {
            const char* function_name = n->func_decl_data.id->identifier_data.name;
            struct dynlib_sym* sym = ctx->find_import_fn(ctx->find_import_fn_userptr, function_name);
            if (!sym)
            {
                printf("unable to find function '%s' for import\n", function_name);
                return 1;
            }

            //just add it as import function, that needs to be resolved later
            struct function func = {
                .location = -1,
                .name = n->func_decl_data.id->identifier_data.name,
                .localvariablesize = 0,
                .variables = hash_map_create(struct variable)
            };
            ctx->function = NULL;
            linked_list_prepend(ctx->functions, func);
        }
    } break;
    
    case AST_BLOCK_STMT:
    {
        //db(ctx, 0xcc); //int3
        linked_list_reversed_foreach(n->block_stmt_data.body, struct ast_node**, it,
        {
            process(ctx, *it);
        });
    } break;

    case AST_EMPTY:
        break;

	case AST_SEQ_EXPR:
	{
		struct ast_node* expr = n->seq_expr_data.expr[0];
		process( ctx, expr );
		cg->push( ctx, EAX );
		for ( int i = 1; i < n->seq_expr_data.numexpr; ++i )
		{
			process( ctx, n->seq_expr_data.expr[i] );
		}
		cg->pop( ctx, EAX );
	}
	break;

	case AST_PROGRAM:
    {
		linked_list_reversed_foreach( n->program_data.body, struct ast_node**, it, {
                process(ctx, (*it));
        } );
    } break;

	case AST_DO_WHILE_STMT:
	{
        struct scope scope;
        enter_scope(ctx, &scope);
        int jmp_beg = instruction_position(ctx);
        
        process(ctx, n->while_stmt_data.body);
        rvalue(ctx, EAX, n->while_stmt_data.test);
        
		// test eax,eax
		db( ctx, 0x85 );
		db( ctx, 0xc0 );

        int jmp_end = instruction_position(ctx);
        
		// jnz rel32
		db( ctx, 0x0f );
		db( ctx, 0x85 );
		dd( ctx,  jmp_beg - jmp_end - 6 );
        
        for(int i = 0; i < scope.numbreaks; ++i)
			set32( ctx, scope.breaks[i] + 1, instruction_position( ctx ) - scope.breaks[i] - 5 );
		exit_scope(ctx);
	} break;
    
    case AST_WHILE_STMT:
	{
        struct scope scope;
        enter_scope(ctx, &scope);
        int pos = instruction_position(ctx);
        rvalue(ctx, EAX, n->while_stmt_data.test);
        
		// test eax,eax
		db( ctx, 0x85 );
		db( ctx, 0xc0 );
        
		// jz rel32
		int jz_pos = instruction_position( ctx ); // jmp_pos + 2 = new_pos
		db( ctx, 0x0f );
		db( ctx, 0x84 );
		dd( ctx, 0x0 ); // placeholder
        process(ctx, n->while_stmt_data.body);
        int tmp = instruction_position(ctx);
        
        //jmp relative
        db(ctx, 0xe9);
        dd(ctx, pos - tmp - 5);
        
        set32( ctx, jz_pos + 2, instruction_position( ctx ) - jz_pos - 6 );
        for(int i = 0; i < scope.numbreaks; ++i)
			set32( ctx, scope.breaks[i] + 1, instruction_position( ctx ) - scope.breaks[i] - 5 );
		exit_scope(ctx);
	} break;

    case AST_BREAK_STMT:
	{
        struct scope *scope = active_scope(ctx);
        assert(scope);
		assert(scope->break_cond);
		cg->jmp_end(ctx, scope->break_cond);
	} break;

	case AST_FOR_STMT:
    {
        struct scope scope = {0};
        enter_scope(ctx, &scope);
        if(n->for_stmt_data.init)
            process(ctx, n->for_stmt_data.init);
        
		reljmp_t rel_loop;
		cg->jmp_begin(ctx, &rel_loop, RJ_JMP | RJ_REVERSE);
		
		reljmp_t rel;
		scope.break_cond = &rel;
		if(n->for_stmt_data.test)
		{
			rvalue( ctx, EAX, n->for_stmt_data.test );
			cg->test(ctx, EAX, EAX);
			cg->jmp_begin(ctx, &rel, RJ_JZ);
		}

		if(n->for_stmt_data.body)
            process(ctx, n->for_stmt_data.body);
        if(n->for_stmt_data.update)
            process(ctx, n->for_stmt_data.update);
		
		cg->jmp_end(ctx, &rel_loop);
		
        if(n->for_stmt_data.test)
		{
			cg->jmp_end(ctx, &rel);
		}
		exit_scope(ctx);
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
        ctx->function->localvariablesize += variable_size;

        int offset = ctx->function->localvariablesize;
        
        struct variable tv = { .offset = offset, .is_param = 0, .data_type_node = data_type_node };
        hash_map_insert( ctx->function->variables, variable_name, tv );

        if(iv)
		{
            struct ast_node c = { .type = AST_ASSIGNMENT_EXPR };
            c.assignment_expr_data.lhs = id;
            c.assignment_expr_data.rhs = iv;
            c.assignment_expr_data.operator = '=';
            process(ctx, &c);
		}
	} break;

    case AST_EXIT:
    {
        //TODO: FIXME
    } break;
    
    default:
        if(rvalue(ctx, EAX, n))
		{
			printf( "unhandled ast node type '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
			exit( -1 );
		}
		break;
    }
}

static void compile_context_print(compiler_t *ctx, const char *format, ...)
{
	//TODO: if verbose
	#if 0
	va_list va;
	va_start(va, format);
	char buf[16384];
	vsprintf(buf, format, va);
	printf("%s\n", buf);
	va_end(va);
	#endif
}

int compile_ast(struct ast_node *head, compiler_t *ctx, codegen_t *cg_)
{
	cg = cg_;
	ctx->print = compile_context_print;
	ctx->rvalue = rvalue;
	ctx->lvalue = lvalue;
    ctx->entry = 0xffffffff;
    ctx->instr = NULL;
    ctx->function = NULL;
    ctx->relocations = linked_list_create(struct relocation);
    ctx->functions = linked_list_create(struct function);
    ctx->data = NULL;
    //empty string
    heap_string_push(&ctx->data, 0);

    memset(ctx->registers, 0, sizeof(ctx->registers));
    ctx->scope_index = 0;
	
	//cg->int3(ctx);
	//cg->int3(ctx);

    switch (ctx->build_target)
    {
    case BT_MEMORY:
        //so we can just call <buf_loc>
		cg->push(ctx, EBP);
		cg->mov(ctx, EBP, ESP);
        break;
    }

	int from;
	cg->mov_r_imm32(ctx, EAX, 0x0, &from);
	cg->call_r32(ctx, EAX);

    switch (ctx->build_target)
    {
    case BT_LINUX_X86:
	case BT_LINUX_X64:
    case BT_OPCODES:
		cg->exit_instr(ctx, EAX);
        break;

    case BT_MEMORY:
		cg->mov(ctx, ESP, EBP);
		cg->pop(ctx, EBP);
		cg->ret(ctx);
        break;

    default:
        perror("unhandled bt");
        break;
    }

    
    process(ctx, head);
    
    struct relocation reloc = {
        .from = from,
        .to = ctx->entry,
        .size = 4,
        .type = RELOC_CODE
    };
    linked_list_prepend(ctx->relocations, reloc);
    return 0;
}
