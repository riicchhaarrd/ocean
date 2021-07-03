#include "ast.h"
#include "types.h"

#include "rhd/heap_string.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"
#include "std.h"

enum REGISTERS
{
    EAX,
    EBX,
    ECX,
    EDX,
    ESI,
    ESP,
    EBP,
    EIP
};

struct variable
{
    int offset;
};

struct relocation
{
    int type; //FIXME: unused for now
    int size;
    int from;
    int to;
};

struct compile_context
{
    struct hash_map *variables;
    heap_string data;

    struct linked_list *relocations;
    
	heap_string instr;
    int localsize;
};

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

static void buf(struct compile_context *ctx, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(&ctx->instr, buf[i] & 0xff);
    }
}

static void process(struct compile_context *ctx, struct ast_node *n);

static int function_call_ident(struct compile_context *ctx, const char *function_name, struct ast_node **args, int numargs)
{
    if(!strcmp(function_name, "exit"))
	{
        assert(numargs > 0);
        //maybe later push eax and pop after to preserve the register
        process(ctx, args[0]);
        //insert linux syscall exit
        db(ctx, 0x88); //mov bl, al
        db(ctx, 0xc3);
        db(ctx, 0x31); //xor eax,eax
        db(ctx, 0xc0);
        db(ctx, 0x40); //inc eax
        db(ctx, 0xcd); //int 0x80
        db(ctx, 0x80);
        return 0;
	}
	return 1;
}

static int add_data(struct compile_context *ctx, void *data, u32 data_size)
{
    int curpos = heap_string_size(&ctx->data);
    heap_string_appendn(&ctx->data, data, data_size);
	return curpos;
}

static void process(struct compile_context *ctx, struct ast_node *n)
{
    switch(n->type)
    {
    case AST_BLOCK_STMT:
        linked_list_reversed_foreach(n->block_stmt_data.body, struct ast_node**, it,
        {
            process(ctx, *it);
        });
        break;
    case AST_IDENTIFIER:
	{
        struct variable *var = hash_map_find(ctx->variables, n->identifier_data.name);
        assert(var); //assume the variable exists, otherwise return a compiler error... FIXME

        //FIXME: don't assume that it's only integer values.. lookup the variable and check the type and handle it accordingly

        //mov eax,[ebp-4]
        db(ctx, 0x8b);
        db(ctx, 0x45);
        db(ctx, 0xfc - 4 * var->offset);
	} break;
    case AST_LITERAL:        
        //mov eax,imm32
        switch(n->literal_data.type)
        {
        case LITERAL_INTEGER:
            db(ctx, 0xb8);
            dd(ctx, n->literal_data.integer);
            break;
        case LITERAL_STRING:
        {
            db(ctx, 0xb8);
            int from = instruction_position(ctx);
            dd(ctx, 0xcccccccc); //placeholder
            const char *str = n->literal_data.string;
            int sz = strlen(str) + 1;
            int to = add_data(ctx, (void*)str, sz);
            
            //TODO: FIXME make it cleaner and add just a function call before the placeholder inject/xref something
            //and make it work with any type of data so it can go into the .data segment

            struct relocation reloc = {
                .from = from,
                .to = to,
                .size = sz
            };
            linked_list_prepend(ctx->relocations, reloc);
        } break;
        default:
            perror("unhandled literal");
            break;
        }
        break;
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
            process(ctx, arg);
            switch(n->unary_expr_data.operator)
            {
            case '-':
                //neg eax
                db(ctx, 0xf7);
                db(ctx, 0xd8);
                break;
            case '!':
            case '~':
                //not eax
                db(ctx, 0xf7);
                db(ctx, 0xd0);
                if(n->unary_expr_data.operator=='!')
                {
                    //and eax,1
					db(ctx, 0x83);
					db(ctx, 0xe0);
					db(ctx, 0x01);
                }
                break;
                
            default:
                printf("unhandled unary expression %c\n", n->unary_expr_data.operator);
                break;
            }
        }
    } break;
    
    case AST_BIN_EXPR:
    {
        struct ast_node *lhs = n->bin_expr_data.lhs;
        struct ast_node *rhs = n->bin_expr_data.rhs;

        //eax should still be stored leftover
        if(lhs->type == AST_LITERAL)
        {
            //mov eax,imm32
            db(ctx, 0xb8);
            dd(ctx, lhs->literal_data.integer);
        } else
            process(ctx, lhs);
        
        if(rhs->type == AST_LITERAL)
        {
            //mov ecx,imm32
            db(ctx, 0xb9);
            dd(ctx, rhs->literal_data.integer);
        } else
        {
            //push eax
            db(ctx, 0x50);
            process(ctx, rhs);
            //mov ecx,eax
            db(ctx, 0x89);
            db(ctx, 0xc1);
            //pop eax
            db(ctx, 0x58);
        }

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

        default:
            printf("unhandled operator %c\n", n->bin_expr_data.operator);
            break;
        }

        //mov eax,edx
        //db(ctx, 0x89);
        //db(ctx, 0xd0);
            
    } break;

    case AST_ASSIGNMENT_EXPR:
    {
        //allocate some space

        //sub esp, 4
        db(ctx, 0x83);
        db(ctx, 0xec);
        db(ctx, 0x04);
        
        struct ast_node *lhs = n->assignment_expr_data.lhs;
        assert(lhs->type == AST_IDENTIFIER);
        
        struct ast_node *rhs = n->assignment_expr_data.rhs;
        process(ctx, rhs);
        //we should now have our result in eax
        
        switch(n->assignment_expr_data.operator)
        {
        case '=':
        {
            //lea ebx,[ebp-4]
            db(ctx, 0x8d);
            db(ctx, 0x5d);

            //mov [ebp-4], eax
            //db(ctx, 0x89);
            //db(ctx, 0x85);
            
            struct variable *var = hash_map_find(ctx->variables, lhs->identifier_data.name);
            if(!var)
            {
    	        //db(ctx, 0xfc - 4 * ctx->localsize++);
            	db(ctx, 0xfc - 4 * ctx->localsize++);
                
                hash_map_insert(ctx->variables, lhs->identifier_data.name, (struct variable) {
                        .offset = ctx->localsize - 1
                });
            }
            else
            {
            	db(ctx, 0xfc - 4 * var->offset);
	            //db(ctx, 0xfc - 4 * var->offset);
            }
            
            //db(ctx, 0xff);
            //db(ctx, 0xff);
            //db(ctx, 0xff);
            //db(ctx, 0xff);
            //db(ctx, 0xff);
            //db(ctx, 0xff);
            //mov [ebx],eax
            db(ctx, 0x89);
            db(ctx, 0x03);
        } break;
        
        default:
            printf("unhandled assignment operator\n");
            break;
        }
        //TODO: cleanup local variables
    } break;

    case AST_FUNCTION_CALL_EXPR:
    {
        struct ast_node **args = n->call_expr_data.arguments;
        int numargs = n->call_expr_data.numargs;
        struct ast_node *callee = n->call_expr_data.callee;

        if(callee->type == AST_IDENTIFIER)
		{
            int ret = function_call_ident(ctx, callee->identifier_data.name, args, numargs);
            if(ret)
			{
                FIXME("cannot find function '%s'\n", callee->identifier_data.name);
			}
		} else
		{
            FIXME("unhandled function call expression callee type");
		}
	} break;

    case AST_EXIT:
    {
        //TODO: FIXME
    } break;
    
    default:
		printf("unhandled ast node type %d\n", n->type);
        break;
    }
}

heap_string x86(struct ast_node *head)
{
    struct compile_context ctx = {
		.instr = NULL,
        .localsize = 0,
        .variables = NULL,
        .relocations = NULL
    };

    ctx.relocations = linked_list_create(struct relocation);
    ctx.variables = hash_map_create(struct variable);
    ctx.data = NULL;
    
    //push ebp
    //mov ebp, esp
    db(&ctx, 0x55);
    db(&ctx, 0x89);
    db(&ctx, 0xe5);
    process(&ctx, head);

    //mov esp,ebp
    //pop ebp
    db(&ctx, 0x89);
    db(&ctx, 0xec);
    db(&ctx, 0x5d);

    heap_string_free(&ctx.data);
    hash_map_destroy(&ctx.variables);
    linked_list_destroy(&ctx.relocations);
    return ctx.instr;
}
