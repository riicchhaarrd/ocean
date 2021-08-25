#include "ast.h"
#include "types.h"
#include "token.h"

#include "std.h"
#include "compile.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"

struct ast_node *get_struct_member_info(struct compile_context* ctx, struct ast_struct_decl *decl, const char *member_name, int *offset, int *size);

int instruction_position(struct compile_context *ctx)
{
    return heap_string_size(&ctx->instr);
}

static void dd(struct compile_context *ctx, u32 i)
{
    union
    {
        uint32_t i;
        uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
		heap_string_push(&ctx->instr, u.b[i]);
}

static void dw(struct compile_context *ctx, u16 i)
{
    union
    {
        uint16_t s;
        uint8_t b[2];
    } u = { .s = i };

    heap_string_push(&ctx->instr, u.b[0]);
    heap_string_push(&ctx->instr, u.b[1]);
}

static void db(struct compile_context *ctx, u8 op)
{
    heap_string_push(&ctx->instr, op);
}

static void set8(struct compile_context *ctx, int offset, u8 op)
{
    ctx->instr[offset] = op;
}

static void set32(struct compile_context *ctx, int offset, u32 value)
{
    u32 *ptr = (u32*)&ctx->instr[offset];
    *ptr = value;
}

static void buf(struct compile_context *ctx, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(&ctx->instr, buf[i] & 0xff);
    }
}

int get_function_position(struct compile_context *ctx, const char *name)
{    
    linked_list_reversed_foreach(ctx->functions, struct function*, it,
    {
        if(!strcmp(it->name, name))
            return it->location;
    });
    return -1;
}

static int primitive_data_type_size(int type)
{
    switch(type)
	{
    case DT_CHAR: return IMM8;
    case DT_SHORT: return IMM16;
    case DT_INT: return IMM32;
    case DT_LONG: return IMM32;
    case DT_NUMBER: return IMM32;
    case DT_FLOAT: return IMM32;
    case DT_DOUBLE: return IMM32;
    case DT_VOID: return 0;
	}
    debug_printf("unhandled type %d\n", type);
    return 0;
}

static int data_type_size(struct compile_context *ctx, struct ast_node *n)
{
    switch(n->type)
	{
    case AST_STRUCT_DATA_TYPE:
    {
        int total = 0;
        struct ast_node *ref = n->struct_data_type_data.struct_ref;
        assert(ref);
        for(int i = 0; i < ref->struct_decl_data.numfields; ++i)
            total += data_type_size(ctx, ref->struct_decl_data.fields[i]->variable_decl_data.data_type);
        return total;
	} break;
    case AST_IDENTIFIER:
	{
		struct variable* var = hash_map_find( ctx->function->variables, n->identifier_data.name );
		assert( var );
        return data_type_size(ctx, var->data_type_node);
	}
	break;
	case AST_POINTER_DATA_TYPE:
        return IMM32;
    case AST_PRIMITIVE_DATA_TYPE:
        return primitive_data_type_size(n->primitive_data_type_data.primitive_type);
    case AST_ARRAY_DATA_TYPE:
	{
		assert( n->array_data_type_data.array_size > 0 );

		if ( n->array_data_type_data.data_type->type == AST_ARRAY_DATA_TYPE )
			return data_type_size( ctx, n->array_data_type_data.data_type ) * n->array_data_type_data.array_size;
		else if ( n->array_data_type_data.data_type->type == AST_PRIMITIVE_DATA_TYPE )
		{
            //printf("array size = %d, primitive_type_size = %d\n", n->array_data_type_data.array_size, primitive_data_type_size(  n->array_data_type_data.data_type->primitive_data_type_data.primitive_type ));
			return primitive_data_type_size( n->array_data_type_data.data_type->primitive_data_type_data.primitive_type ) *
				   n->array_data_type_data.array_size;
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
    if(n->type == AST_PRIMITIVE_DATA_TYPE || n->type == AST_POINTER_DATA_TYPE)
        return 0;
    //for now pass everything else by reference, arrays etc
    return 1;
}

//TODO: implement all opcodes we'll be using so we can keep track of the registers and their values
//TODO: replace our "real" registers with "virtual" registers

static void push(struct compile_context *ctx, enum REGISTER reg)
{
    db(ctx, 0x50 + reg);
    ctx->registers[ESP] -= 4;
}

static void inc(struct compile_context *ctx, enum REGISTER reg)
{
    db(ctx, 0x40 + reg);
}

static void pop(struct compile_context *ctx, enum REGISTER reg)
{
    db(ctx, 0x58 + reg);
    ctx->registers[ESP] += 4;
}

static void mov_r_imm32(struct compile_context *ctx, enum REGISTER reg, i32 imm)
{
    ctx->registers[reg] = imm;
    db(ctx, 0xb8 + reg);
    dd(ctx, imm);
}

static void add(struct compile_context *ctx, enum REGISTER a, enum REGISTER b)
{
    ctx->registers[a] += ctx->registers[b];
    db(ctx, 0x01);
    db(ctx, 0xc0 + b * 9 + a);
}

static void xor(struct compile_context *ctx, enum REGISTER a, enum REGISTER b)
{
    assert(a == EAX && b == EAX);
    ctx->registers[a] ^= ctx->registers[b];
	db( ctx, 0x31 );
	db( ctx, 0xc0 + b * 9 + a );
}

static void sub(struct compile_context *ctx, enum REGISTER a, enum REGISTER b)
{
    ctx->registers[a] += ctx->registers[b];
    db(ctx, 0x29);
    db(ctx, 0xc0 + b * 9 + a);
}

static int add_data(struct compile_context *ctx, void *data, u32 data_size)
{
    int curpos = heap_string_size(&ctx->data);
    heap_string_appendn(&ctx->data, data, data_size);
	return curpos;
}

static void mov_r_string(struct compile_context *ctx, enum REGISTER reg, const char *str)
{
	db( ctx, 0xb8 + reg);
	int from = instruction_position( ctx );
	dd( ctx, 0xcccccccc ); // placeholder

    //TODO: FIXME reloc the register we're keeping track of aswell to the correct to be determined memory location
    ctx->registers[reg] = (intptr_t)str;
    int to, sz;
    if(strlen(str) > 0)
	{
		sz = strlen( str ) + 1;
		to = add_data( ctx, (void*)str, sz );
	} else
	{
		sz = 0;
        to = 0;
	}

	// TODO: FIXME make it cleaner and add just a function call before the placeholder inject/xref something
	// and make it work with any type of data so it can go into the .data segment

	struct relocation reloc = { .from = from, .to = to, .size = sz, .type = RELOC_DATA };
	linked_list_prepend( ctx->relocations, reloc );
}

static void process(struct compile_context *ctx, struct ast_node *n);

static int function_call_ident(struct compile_context *ctx, const char *function_name, struct ast_node **args, int numargs)
{
    //printf("func call %s\n", function_name);
    if(!strcmp(function_name, "syscall") && ctx->build_target == BT_LINUX)
    {
        int rvalue(struct compile_context *ctx, enum REGISTER reg, struct ast_node *n);
        assert(numargs > 0);

        for(int i = 0; i < 6 - numargs; ++i)
		{
			xor( ctx, EAX, EAX );
			push( ctx, EAX );
		}
		for(int i = 0; i < numargs; ++i)
		{
			rvalue( ctx, EAX, args[numargs-i-1] );
			push( ctx, EAX );
		}
        
        //TODO: FIXME arg5 (%ebp) is on the stack //ebp
        pop(ctx, EAX);
        pop(ctx, EBX);
        pop(ctx, ECX);
        pop(ctx, EDX);
        pop(ctx, ESI);
        pop(ctx, EDI);

        /*
        for(int i = 0; i < numargs; ++i)
		{
            printf("arg%d=%s (var=%s)\n",i,AST_NODE_TYPE_to_string(args[i]->type),args[i]->type==AST_IDENTIFIER?args[i]->identifier_data.name : "[n/a]");
		}
        */

		db(ctx, 0xcd); //int 0x80
        db(ctx, 0x80);
        return 0;
    } else if(!strcmp(function_name, "int3"))
	{
        if(opt_flags & OPT_DEBUG)
        db(ctx, 0xcc); //int3
        return 0;
	}

    int pos = get_function_position(ctx, function_name);
    if(pos == -1)
    {
        db(ctx, 0xcc);
        db(ctx, 0xcc);
        db(ctx, 0xcc);
		return 1;
    }

	for(int i = 0; i < numargs; ++i)
    {
		process( ctx, args[numargs-i-1] );
        //push eax
        db(ctx, 0x50);
    }
    
    int t = instruction_position(ctx);
    db(ctx, 0xe8);
    dd(ctx, pos - t - 5);

    if(numargs > 0)
	{
		// add esp, 4
		db( ctx, 0x83 );
		db( ctx, 0xc4 );
		db( ctx, numargs * 4 );
	}
	return 0;
}

static int function_variable_declaration_stack_size( struct compile_context* ctx, struct ast_node* n )
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
static int accumulate_local_variable_declaration_size(struct compile_context *ctx, struct ast_node *n)
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

static intptr_t register_value(struct compile_context *ctx, enum REGISTER reg)
{
    return ctx->registers[reg];
}

static struct ast_node *identifier_data_node(struct compile_context *ctx, struct ast_node *n)
{
    if(n->type != AST_IDENTIFIER)
        debug_printf("expected identifier, got '%s'\n", AST_NODE_TYPE_to_string(n->type));
	assert(n->type == AST_IDENTIFIER);
    struct variable *var = hash_map_find(ctx->function->variables, n->identifier_data.name);
    assert(var);
    return var->data_type_node;
}

static int data_type_operand_size(struct compile_context *ctx, struct ast_node *n, int ptr)
{
	switch ( n->type )
	{
	// not a datatype, but we'll resolve it
	case AST_IDENTIFIER:
        return data_type_operand_size(ctx, identifier_data_node(ctx, n), ptr);

	case AST_PRIMITIVE_DATA_TYPE:
		return primitive_data_type_size( n->primitive_data_type_data.primitive_type );
        
#ifndef MIN
#define MIN( a, b ) ( ( a ) > ( b ) ? ( b ) : ( a ) )
#endif
	case AST_BIN_EXPR:
        return MIN( data_type_operand_size(ctx, n->bin_expr_data.lhs, ptr), data_type_operand_size(ctx, n->bin_expr_data.rhs, ptr) );

    case AST_LITERAL:
        return IMM32;

	case AST_POINTER_DATA_TYPE:
		if ( ptr )
			return IMM32;
		if ( n->pointer_data_type_data.data_type->type == AST_POINTER_DATA_TYPE )
			return data_type_operand_size( ctx, n->pointer_data_type_data.data_type, 1 );
		else
			return primitive_data_type_size(
				n->pointer_data_type_data.data_type->primitive_data_type_data.primitive_type );

	case AST_ARRAY_DATA_TYPE:
		return primitive_data_type_size( n->array_data_type_data.data_type->primitive_data_type_data.primitive_type );

    case AST_STRUCT_MEMBER_EXPR:
    {
		struct ast_node* idn = identifier_data_node(ctx, n->member_expr_data.object);
        //ast_print_node_type("idn", idn);
		assert(idn->type == AST_STRUCT_DATA_TYPE || idn->type == AST_POINTER_DATA_TYPE);
        struct ast_node *sr = NULL;
        if(idn->type == AST_POINTER_DATA_TYPE)
            sr = idn->pointer_data_type_data.data_type->struct_data_type_data.struct_ref;
        else
            sr = idn->struct_data_type_data.struct_ref;
            
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
	}
	debug_printf( "unhandled data type '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
	return 0;
}

int rvalue(struct compile_context *ctx, enum REGISTER reg, struct ast_node *n);
int lvalue(struct compile_context *ctx, enum REGISTER reg, struct ast_node *n);
void store_operand(struct compile_context *ctx, struct ast_node *n);
static void ast_handle_assignment_expression( struct compile_context* ctx, struct ast_node* n )
{
    push(ctx, EBX);
	struct ast_node* lhs = n->assignment_expr_data.lhs;
	struct ast_node* rhs = n->assignment_expr_data.rhs;

	rvalue( ctx, EAX, rhs );
	// we should now have our result in eax

	switch ( n->assignment_expr_data.operator)
	{
	case TK_PLUS_ASSIGN:
	{
		push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		// load_variable(ctx, EBX, 1, lhs, 0);
		pop( ctx, EAX );

		// add [ebx],eax
		db( ctx, 0x01 );
		db( ctx, 0x03 );
	}
	break;
	case TK_MINUS_ASSIGN:
	{
		push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		// load_variable(ctx, EBX, 1, lhs, 0);
		pop( ctx, EAX );
		// sub [ebx],eax
		db( ctx, 0x29 );
		db( ctx, 0x03 );
	}
	break;
		// TODO: add mul and other operators

	case TK_MOD_ASSIGN:
	{
		// mov esi, eax
		db( ctx, 0x89 );
		db( ctx, 0xc6 );

		// TODO: maybe push esi, incase we trash the register
		rvalue( ctx, EAX, lhs );

		// xor edx,edx
		db( ctx, 0x31 );
		db( ctx, 0xd2 );

		// idiv esi
		db( ctx, 0xf7 );
		db( ctx, 0xfe );

		push( ctx, EDX );
		lvalue( ctx, EBX, lhs );
		pop( ctx, EDX );

		// mov [ebx],edx
		db( ctx, 0x89 );
		db( ctx, 0x13 );
	}
	break;

	case TK_DIVIDE_ASSIGN:
	{
		// mov esi, eax
		db( ctx, 0x89 );
		db( ctx, 0xc6 );

		// TODO: maybe push esi, incase we trash the register
		rvalue( ctx, EAX, lhs );

		// xor edx,edx
		db( ctx, 0x31 );
		db( ctx, 0xd2 );

		// idiv esi
		db( ctx, 0xf7 );
		db( ctx, 0xfe );

		push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		pop( ctx, EAX );

		// mov [ebx],eax
		db( ctx, 0x89 );
		db( ctx, 0x03 );
	}
	break;

	case TK_MULTIPLY_ASSIGN:
	{
		// mov esi, eax
		db( ctx, 0x89 );
		db( ctx, 0xc6 );

		// TODO: maybe push esi, incase we trash the register
		rvalue( ctx, EAX, lhs );

		// xor edx,edx
		db( ctx, 0x31 );
		db( ctx, 0xd2 );

		// imul esi
		db( ctx, 0xf7 );
		db( ctx, 0xee );

		push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		pop( ctx, EAX );

		// mov [ebx],eax
		db( ctx, 0x89 );
		db( ctx, 0x03 );
	}
	break;

	case '=':
	{
		int os = data_type_operand_size( ctx, lhs, 1 );
		push( ctx, EAX );
		lvalue( ctx, EBX, lhs );
		pop( ctx, EAX );
		store_operand( ctx, lhs );
	}
	break;

	default:
		printf( "unhandled assignment operator\n" );
		break;
	}
    pop(ctx, EBX);
}


void store_operand(struct compile_context *ctx, struct ast_node *n)
{
	int os = data_type_operand_size( ctx, n, 1 );
	// TODO: fix hardcoded EBX
	switch ( os )
	{
		case IMM32:
			// mov [ebx],eax
		db( ctx, 0x89 );
		db( ctx, 0x03 );
		break;
    case IMM16:
		// mov word ptr [ebx], ax
		db( ctx, 0x66 );
		db( ctx, 0x89 );
		db( ctx, 0x03 );
        break;
	case IMM8:
		// mov byte ptr [ebx], al
		db( ctx, 0x88 );
			db( ctx, 0x03 );
			break;
		default:
			debug_printf( "unhandled operand size %d\n", os );
			exit( 1 );
		break;
	}
}

static int load_operand(struct compile_context *ctx, struct ast_node *n)
{
	int os = data_type_operand_size( ctx, n, 0 );
	switch ( os )
	{
	case IMM32:
		// mov eax, [ebx]
		db( ctx, 0x8b );
		db( ctx, 0x03 );
		break;
    case IMM16:
		// movzx eax, word [ebx]
		db( ctx, 0x0f );
		db( ctx, 0xb7 );
		db( ctx, 0x03 );
        break;
	case IMM8:
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

int rvalue(struct compile_context *ctx, enum REGISTER reg, struct ast_node *n)
{
    //printf("rvalue node '%s'\n", AST_NODE_TYPE_to_string(n->type));
    switch(n->type)
	{
	case AST_LITERAL:
		// mov eax,imm32
		switch ( n->literal_data.type )
		{
		case LITERAL_INTEGER:
            mov_r_imm32(ctx, reg, n->literal_data.integer);
			break;
		case LITERAL_STRING:
		{
            mov_r_string(ctx, reg, n->literal_data.string);
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

            // handles only byte size offset
			//db( ctx, 0x8d );
			//db( ctx, 0x45 + 8 * reg );
			//db( ctx, offset );

			// lea r32,[ebp - offset]
			db(ctx, 0x8d);
			db(ctx, 0x85 + 8 * reg);
			dd(ctx, offset);
			break;
            
        default:
		{
			int os = data_type_operand_size( ctx, n, 1 );
            switch(os)
			{
            case IMM32:
				/* // mov r32,[ebp - offset] */
				/* db( ctx, 0x8b ); */
				/* db( ctx, 0x45 + 8 * reg ); */
				/* db( ctx, offset ); */
                
				// mov r32,[ebp - offset]
				db(ctx, 0x8b);
				db(ctx, 0x85 + 8 * reg);
				dd(ctx, offset);
				break;
            case IMM16:
            case IMM8:
                push(ctx,EBX);
                lvalue(ctx,EBX,n);
                load_operand(ctx, n);
                pop(ctx,EBX);
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
		// test eax,eax
		db( ctx, 0x85 );
		db( ctx, 0xc0 );

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
        
        push( ctx, EAX );
		rvalue( ctx, EAX, rhs );
		// mov ecx,eax
		db( ctx, 0x89 );
		db( ctx, 0xc1 );
        
		pop( ctx, EAX );

        //xor edx,edx
        db(ctx, 0x31);
        db(ctx, 0xd2);

        //xor edx,edx
        //db(ctx, 0x31);
        //db(ctx, 0xd2);

        switch(n->bin_expr_data.operator)
        {
        case '*':
            //imul ecx
            db(ctx, 0xf7);
            db(ctx, 0xe9);
            break;
        case '/':
            //idiv ecx
            db(ctx, 0xf7);
            db(ctx, 0xf9);
            break;

        case '+':
            db(ctx, 0x01);
            db(ctx, 0xc8);
            break;
        case '-':
            db(ctx, 0x29);
            db(ctx, 0xc8);
            break;
        case '&':
            db(ctx, 0x21);
            db(ctx, 0xc8);
            break;
            break;
        case '|':
            db(ctx, 0x09);
            db(ctx, 0xc8);
            break;
        case '^':
            db(ctx, 0x31);
            db(ctx, 0xc8);
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
            db(ctx, 0xf7);
            db(ctx, 0xf9);
            db(ctx, 0x89);
            db(ctx, 0xd0);
            break;

        case TK_GEQUAL:
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jl <relative offset>
            db(ctx, 0x7c);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            break;
            
        case TK_LEQUAL:
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jg <relative offset>
            db(ctx, 0x7f);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            break;
            
        case '>':
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jle <relative offset>
            db(ctx, 0x7e);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            break;
            
        case '<':
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jge <relative offset>
            db(ctx, 0x7d);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            break;
            
        case TK_EQUAL:
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jne <relative offset>
            db(ctx, 0x75);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);            
            break;
            
        case TK_NOT_EQUAL:
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //je <relative offset>
            db(ctx, 0x74);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);            
            break;

        default:
            printf("unhandled operator (%d) %c\n", n->bin_expr_data.operator, n->bin_expr_data.operator);
            break;
        }

        //mov eax,edx
        //db(ctx, 0x89);
        //db(ctx, 0xd0);
            
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
        case AST_PRIMITIVE_DATA_TYPE:
        case AST_POINTER_DATA_TYPE:
        case AST_ARRAY_DATA_TYPE:
        case AST_STRUCT_DATA_TYPE:
			sz = data_type_size( ctx, n->sizeof_data.subject );
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

        //mov eax,imm32
        db(ctx, 0xb8);
        dd(ctx, sz);
	} break;

    case AST_STRUCT_MEMBER_EXPR:
    {
        push(ctx, EBX);
        struct ast_node *object = n->member_expr_data.object;
		struct ast_node *dn = identifier_data_node(ctx, object);
        //ast_print_node_type("object", dn);
		if ( dn->type == AST_POINTER_DATA_TYPE )
        {
			rvalue( ctx, EAX, object );

			// mov ebx, eax
			db( ctx, 0x89 );
			db( ctx, 0xc3 );
		}
		else
			lvalue( ctx, EBX, object );
        
		struct ast_node* sr = NULL;
		if (dn->type == AST_POINTER_DATA_TYPE)
			sr = dn->pointer_data_type_data.data_type->struct_data_type_data.struct_ref;
		else
			sr = dn->struct_data_type_data.struct_ref;

		assert(n->member_expr_data.property->type == AST_IDENTIFIER);
		int off, sz;
		struct ast_node *field = get_struct_member_info(ctx, &sr->struct_decl_data, n->member_expr_data.property->identifier_data.name, &off,
							   &sz);
		assert(sz > 0);

		// add ebx, imm32
		db(ctx, 0x81);
		db(ctx, 0xc3);
		dd(ctx, off);

		load_operand(ctx, field->variable_decl_data.data_type);
        pop(ctx, EBX);
	} break;    

    case AST_MEMBER_EXPR:
    {
        push(ctx, EBX);
        struct ast_node *object = n->member_expr_data.object;
        struct ast_node *property = n->member_expr_data.property;
		struct ast_node *dn = identifier_data_node(ctx, object);
        //ast_print_node_type("object", dn);
		if ( dn->type == AST_POINTER_DATA_TYPE )
        {
			rvalue( ctx, EAX, object );

			// mov ebx, eax
			db( ctx, 0x89 );
			db( ctx, 0xc3 );
		}
		else
			lvalue( ctx, EBX, object );
        
        rvalue( ctx, reg, n->member_expr_data.property );
        
		int os = data_type_operand_size( ctx, object, 0 );

		push( ctx, ESI );
        
		// mov esi, imm32
		db( ctx, 0xbe );
		dd( ctx, os );
        
		// imul esi
		db( ctx, 0xf7 );
		db( ctx, 0xee );
        
		pop( ctx, ESI );
        
        add( ctx, EBX, reg );
        load_operand(ctx, dn);
        pop(ctx, EBX);
	} break;
    
    case AST_UNARY_EXPR:
    {
        struct ast_node *arg = n->unary_expr_data.argument;
        if(arg->type == AST_LITERAL)
        {
            switch(n->unary_expr_data.operator)
            {
            case '-':
                //neg eax
                //db(ctx, 0xf7);
                //db(ctx, 0xd8);
                
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, -arg->literal_data.integer);
                break;
                
            case '+':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, arg->literal_data.integer);
                break;

            case '!':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, !arg->literal_data.integer);
                break;
                
            case '~':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, ~arg->literal_data.integer);
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
						push( ctx, EBX );
						// let's assume pparg is a identifier of type const char *
						lvalue( ctx, EBX, pparg );
                        
                        //save our ptr to our variable to edx
						push(ctx, EDX);
						// lea edx,[ebx]
						db( ctx, 0x8d );
						db( ctx, 0x13 );

                        //load the actual char * pointer e.g "test" into ebx
						// mov ebx,[ebx]
						db( ctx, 0x8b );
						db( ctx, 0x1b );

                        //then load our value from ebx (can be a char, int or any type, in this case char 8 bits)
						int os = load_operand( ctx, pparg ); //loads location in EBX into EAX register
                        //printf("os = %d\n", os);
                        
                        //increment the actual pointer value of our string
                        
						// inc [edx]
						db( ctx, 0xff );
						db( ctx, 0x02 );

                        //restore our EDX value if we ever used it for anything else.
                        pop(ctx, EDX);
                        
						pop( ctx, EBX );
					}
					break;

					default:
						debug_printf( "unhandled operator after dereference\n" );
						return 1;
					}
				}
				else
				{
					push( ctx, EBX );
					rvalue( ctx, reg, n->unary_expr_data.argument );
					// mov ebx,eax
					db( ctx, 0x89 );
					db( ctx, 0xc3 );
					load_operand( ctx, n->unary_expr_data.argument );
					pop( ctx, EBX );
				}
			} break;

            case TK_PLUS_PLUS:
				// handle generic ++ case
				push( ctx, EBX );
				lvalue( ctx, EBX, n->unary_expr_data.argument );
				if(!n->unary_expr_data.prefix)
				{
					load_operand( ctx, n->unary_expr_data.argument );
					// inc [ebx]
					db( ctx, 0xff );
					db( ctx, 0x03 );
				} else
				{
					// inc [ebx]
					db( ctx, 0xff );
					db( ctx, 0x03 );
					load_operand( ctx, n->unary_expr_data.argument );
				}
				pop( ctx, EBX );
				break;
			case '&':
                push(ctx, EBX);
				lvalue( ctx, EBX, n->unary_expr_data.argument );
				if ( reg == EAX )
				{
					// mov eax,ebx
					db( ctx, 0x89 );
					db( ctx, 0xd8 );
				}
				pop( ctx, EBX );
				break;
                
			case '-':
                rvalue(ctx, EAX, arg);
                //neg eax
                db(ctx, 0xf7);
                db(ctx, 0xd8);
                break;
            case '!':
            case '~':
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
				break;

			default:
				push( ctx, EBX );
				if ( lvalue( ctx, EBX, n ) )
				{
					debug_printf( "unhandled unary expression '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
					abort();
				}
                load_operand(ctx, n);
				pop( ctx, EBX );
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
        push(ctx, EAX);
        for(int i = 1; i < n->seq_expr_data.numexpr; ++i)
		{
            rvalue(ctx, reg, n->seq_expr_data.expr[i]);
		}
		pop(ctx, EAX);
	} break;

	default:
        push(ctx, EBX);
        if(lvalue(ctx, EBX, n))
		{
			debug_printf( "unhandled rvalue '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
			exit( -1 );
		}
        load_operand(ctx, n);
        pop(ctx, EBX);
		return 1;
	}
    return 0;
}

struct ast_node *get_struct_member_info(struct compile_context* ctx, struct ast_struct_decl *decl, const char *member_name, int *offset, int *size)
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
int lvalue( struct compile_context* ctx, enum REGISTER reg, struct ast_node* n )
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
        
		/* // lea r32,[ebp - offset] */
		/* db( ctx, 0x8d ); */
		/* db( ctx, 0x45 + 8 * reg ); */
		/* db( ctx, offset ); */
        
		// lea r32,[ebp - offset]
		db(ctx, 0x8d);
		db(ctx, 0x85 + 8 * reg);
		dd(ctx, offset);
	} break;
    
	case AST_STRUCT_MEMBER_EXPR:
	{
		struct ast_node* object = n->member_expr_data.object;
		struct ast_node* dn = identifier_data_node(ctx, object);
        switch(dn->type)
		{
		case AST_POINTER_DATA_TYPE:
		case AST_STRUCT_DATA_TYPE:
		{
			struct ast_node* sr = NULL;
			if (dn->type == AST_POINTER_DATA_TYPE)
				sr = dn->pointer_data_type_data.data_type->struct_data_type_data.struct_ref;
			else
				sr = dn->struct_data_type_data.struct_ref;

			assert(n->member_expr_data.property->type == AST_IDENTIFIER);
			int off, sz;
			get_struct_member_info(ctx, &sr->struct_decl_data, n->member_expr_data.property->identifier_data.name, &off,
								   &sz);
			assert(sz > 0);
			// printf("offset = %d, sz = %d for '%s'\n", off, sz, n->member_expr_data.property->identifier_data.name);

			push(ctx, EAX);
			if (dn->type == AST_POINTER_DATA_TYPE)
			{
                //should be good to go for identifiers, most of the rvalue values are still hardcoded to EAX though...
                assert(object->type == AST_IDENTIFIER);
				rvalue(ctx, reg, object);
			}
			else
				lvalue(ctx, reg, object);

			// add ebx, imm32
			db(ctx, 0x81);
			db(ctx, 0xc3);
			dd(ctx, off);

			pop(ctx, EAX);
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
			push(ctx, EAX);
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

			push(ctx, ESI);

			// mov esi, imm32
			db(ctx, 0xbe);
			dd(ctx, os);

			// imul esi
			db(ctx, 0xf7);
			db(ctx, 0xee);

			pop(ctx, ESI);

			add(ctx, EBX, EAX);
			pop(ctx, EAX);

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
			// mov ebx, [ebx]
			db( ctx, 0x8b );
			db( ctx, 0x1b );
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

static struct scope *active_scope(struct compile_context *ctx)
{
    if(ctx->scope_index == 0)
        return NULL;
    return ctx->scope[ctx->scope_index - 1];
}

static void enter_scope(struct compile_context *ctx, struct scope *scope)
{
    assert(ctx->scope_index + 1 < COUNT_OF(ctx->scope));
    scope->numbreaks = 0;
    ctx->scope[ctx->scope_index++] = scope;
}

static void exit_scope(struct compile_context *ctx)
{
    --ctx->scope_index;
    ctx->scope[ctx->scope_index] = NULL;
}

static void process(struct compile_context *ctx, struct ast_node *n)
{
    switch(n->type)
    {
    case AST_EMIT:
    {
        db(ctx, n->emit_data.opcode);
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
		// test eax,eax
		db( ctx, 0x85 );
		db( ctx, 0xc0 );

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
		if ( alternative ) //should work
			process( ctx, alternative );
		set32( ctx, jmp_pos + 1, instruction_position( ctx ) - jmp_pos - 5 );
	}
	break;

	case AST_RETURN_STMT:
    {
        struct ast_node *expr = n->return_stmt_data.argument;
        process(ctx, expr);
        
		//mov esp,ebp
        //pop ebp
        db(ctx, 0x89);
        db(ctx, 0xec);
        db(ctx, 0x5d);
        
        //ret
        db(ctx, 0xc3);   
    } break;
    
    //TODO: implement this properly
    case AST_FUNCTION_DECL:
    {
        if(!strcmp(n->func_decl_data.id->identifier_data.name, "main"))
		{
            //printf("set entry call to 0x%02X (%d)\n", instruction_position( ctx ), instruction_position( ctx ));
			ctx->entry = instruction_position( ctx );
		}
        int loc = instruction_position( ctx );
        struct function func = {
            .location = loc,
            .name = n->func_decl_data.id->identifier_data.name,
            .localvariablesize = 0,
            .variables = hash_map_create(struct variable)
        };
        ctx->function = linked_list_prepend(ctx->functions, func);
        int offset = 0;
        for(int i = 0; i < n->func_decl_data.numparms; ++i)
		{
            struct ast_node *parm = n->func_decl_data.parameters[i];
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
        //push ebp
        //mov ebp, esp
        db(ctx, 0x55);
        db(ctx, 0x89);
        db(ctx, 0xe5);
        
        //allocate some space

        //sub esp, 4
        //works for < 0xff
        //db(ctx, 0x83);
        //db(ctx, 0xec);
        //db(ctx, 0x04);

        //sub esp, imm32
        db(ctx, 0x81);
        db(ctx, 0xec);
        dd(ctx, localsize);
        
        process(ctx, n->func_decl_data.body);
        
		//mov esp,ebp
        //pop ebp
        db(ctx, 0x89);
        db(ctx, 0xec);
        db(ctx, 0x5d);
        
        //ret
        db(ctx, 0xc3);
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
		push( ctx, EAX );
		for ( int i = 1; i < n->seq_expr_data.numexpr; ++i )
		{
			process( ctx, n->seq_expr_data.expr[i] );
		}
		pop( ctx, EAX );
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
        
		int pos = instruction_position( ctx ); // jmp_pos + 1 = new_pos
		db( ctx, 0xe9 );
		dd( ctx, 0x0 ); // placeholder
		scope->breaks[scope->numbreaks++] = pos;
	} break;

	case AST_FOR_STMT:
    {
        struct scope scope;
        enter_scope(ctx, &scope);
        if(n->for_stmt_data.init)
            process(ctx, n->for_stmt_data.init);
        
        int pos = instruction_position(ctx);
        if(n->for_stmt_data.test)
		{
			rvalue( ctx, EAX, n->for_stmt_data.test );
			// test eax,eax
			db( ctx, 0x85 );
			db( ctx, 0xc0 );
		} else
		{
			db( ctx, 0x90 );
			db( ctx, 0x90 );
		}
		// jz rel32
		int jz_pos = instruction_position( ctx ); // jmp_pos + 2 = new_pos

		if(n->for_stmt_data.test)
		{
			db( ctx, 0x0f );
			db( ctx, 0x84 );
			dd( ctx, 0x0 ); // placeholder
		} else
		{
			db( ctx, 0x90 );
			db( ctx, 0x90 );
            
			db( ctx, 0x90 );
			db( ctx, 0x90 );
			db( ctx, 0x90 );
			db( ctx, 0x90 );
		}

		if(n->for_stmt_data.body)
            process(ctx, n->for_stmt_data.body);
        if(n->for_stmt_data.update)
            process(ctx, n->for_stmt_data.update);
        int tmp = instruction_position(ctx);
        
        //jmp relative
        db(ctx, 0xe9);
        dd(ctx, pos - tmp - 5);
        
        if(n->for_stmt_data.test)
			set32( ctx, jz_pos + 2, instruction_position( ctx ) - jz_pos - 6 );
        for(int i = 0; i < scope.numbreaks; ++i)
			set32( ctx, scope.breaks[i] + 1, instruction_position( ctx ) - scope.breaks[i] - 5 );
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

int x86(struct ast_node *head, struct compile_context *ctx)
{
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
    
    //mov eax,imm32
    db(ctx, 0xb8);
    int from = instruction_position(ctx);
    dd(ctx, 0x0);
    
    //call eax
    db(ctx, 0xff);
    db(ctx, 0xd0);
    
    //insert linux syscall exit
    //mov ebx, eax
    db(ctx, 0x89);
    db(ctx, 0xc3);
    //xor ebx,ebx
    //db(ctx, 0x31);
    //db(ctx, 0xdb);
    
    db(ctx, 0x31); //xor eax,eax
    db(ctx, 0xc0);
    db(ctx, 0x40); //inc eax
    db(ctx, 0xcd); //int 0x80
    db(ctx, 0x80);
    
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
