#include "arena.h"
#include "data_type.h"
#include "imm.h"
#include "operand.h"
#include "std.h"
#include "ast.h"
#include "compile.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"
#include "instruction.h"
#include "token.h"
#include "types.h"
#include "virtual_opcodes.h"
#include "register.h"
#include <stdio.h>
//gcc -w -g test.c compile.c ast.c lex.c parse.c && ./a.out

bool rvalue(compiler_t* ctx, ast_node_t* n, voperand_t* dst);
bool compile_visit_node(compiler_t* ctx, ast_node_t* n);

static size_t get_label(compiler_t *ctx)
{
	return ctx->labelindex++;
}

typedef enum
{
	EAX,
	ECX,
	EDX,
	EBX,
	ESP,
	EBP,
	ESI,
	EDI
} x86_registers;

static void register_name(voperand_t op, vregister_t reg, char* buf, size_t maxlen)
{
	if ((op.size == VOPERAND_SIZE_DOUBLE || op.size == VOPERAND_SIZE_FLOAT) && op.type == VOPERAND_REGISTER)
	{
		snprintf(buf, maxlen, "st%d", reg.index);
		return;
	}

	//NOTE: actual order is eax, ecx, edx...
	//but gcc uses edx for some reason over ecx, so i swapped edx and ecx here
	static const char* x86_regnames[] = {"eax", "edx", "ecx", "ebx", "esp", "ebp", "esi", "edi", NULL};
	static const char* fakeregnames[] = {"sp", "bp", "ip", "return_value"};

	if (!op.virtual)
	{
		snprintf(buf, maxlen, "%s", x86_regnames[reg.index]);
		return;
	}

	if (reg.index < COUNT_OF(fakeregnames))
		snprintf(buf, maxlen, "%s", fakeregnames[reg.index]);
	else
		snprintf(buf, maxlen, "r%d", reg.index);
}

static const char* operand_to_string(voperand_t op)
{
	// TODO: FIXME non reentry safe sort of
	static char buffers[4][128];
	static int bufferindex = 0;
	char* buf = &buffers[bufferindex % 4];
	char regname[32];
	switch (op.type)
	{
		case VOPERAND_INDIRECT_REGISTER_DISPLACEMENT:
			register_name(op, op.reg_indirect_displacement.reg, regname, sizeof(regname));
			if(op.reg_indirect_displacement.disp > 0)
			{
				snprintf(buf, 128, "%s [%s + %d]", voperand_size_names[op.size], regname,
						 op.reg_indirect_displacement.disp);
			}
			else
			{
				snprintf(buf, 128, "%s [%s - %d]", voperand_size_names[op.size], regname,
						 -op.reg_indirect_displacement.disp);
			}
			break;
		case VOPERAND_LABEL:
			snprintf(buf, 128, "<sub_%d>", op.label);
			break;
		case VOPERAND_REGISTER:
			register_name(op, op.reg, regname, sizeof(regname));
			snprintf(buf, 128, "%s", regname);
			break;
		case VOPERAND_INDIRECT_REGISTER:
			register_name(op, op.reg, regname, sizeof(regname));
			snprintf(buf, 128, "[%s]", regname);
			break;
		case VOPERAND_IMMEDIATE:
			if (op.size == VOPERAND_SIZE_32_BITS)
				snprintf(buf, 128, "%d", op.imm.dw);
			else if(op.size == VOPERAND_SIZE_DOUBLE)
			{
				double as_dbl;
				memcpy(&as_dbl, &op.imm.dq, sizeof(as_dbl));
				snprintf(buf, 128, "%lfd", as_dbl);
			}
			else if(op.size == VOPERAND_SIZE_FLOAT)
			{
				double as_flt;
				memcpy(&as_flt, &op.imm.dw, sizeof(as_flt));
				snprintf(buf, 128, "%ff", as_flt);
			}
			else
				snprintf(buf, 128, "%lld", op.imm.dq);
			break;
		default:
			snprintf(buf, 128, "%s", voperand_type_strings[op.type]);
			break;
	}
	++bufferindex;
	return buf;
}

static void print_instruction_operand(voperand_t* vo, bool last)
{
	if (vo->type == VOPERAND_INVALID)
		printf("invalid");
	else
		printf("%s%s ", operand_to_string(*vo), last ? "," : "");
}

vregister_t get_vreg(compiler_t *ctx)
{
	vregister_t vr;
	vr.index = ctx->vregindex++;
	/* vr.usage = VRU_GENERAL_PURPOSE; */
	return vr;
}

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

static vinstr_t *emit_instruction(compiler_t* ctx, vopcode_t op)
{
	vinstr_t* instr = &ctx->function->instructions[ctx->function->instruction_index++];
	/* instr->index = ctx->function->instruction_index - 1; */
	instr->opcode = op;
	instr->numoperands = 0;
	for (size_t i = 0; i < COUNT_OF(instr->operands); ++i)
		instr->operands[0] = invalid_operand();
	return instr;
}

static void set_operand(vinstr_t* instr, size_t index, voperand_t op)
{
	instr->operands[index] = op;
}

static vinstr_t* emit_instruction1(compiler_t* ctx, vopcode_t opcode, voperand_t a)
{
	vinstr_t *instr = emit_instruction(ctx, opcode);
	set_operand(instr, 0, a);
	instr->numoperands = 1;
	return instr;
}

static vopcache_t *find_vopcache_by_key(compiler_t *ctx, voperand_t *key)
{
	for(size_t i = 0; i < ctx->function->vopcacheindex; ++i)
	{
		if(voperand_type_equal(&ctx->function->vopcache[i].key, key))
		{
			return &ctx->function->vopcache[i];
		}
	}
	return NULL;
}

static vinstr_t* emit_instruction2(compiler_t* ctx, vopcode_t opcode, voperand_t a, voperand_t b)
{
	vinstr_t* instr = emit_instruction(ctx, opcode);
	set_operand(instr, 0, a);
	set_operand(instr, 1, b);
	instr->numoperands = 2;
	/* if (vopcode_overwrites_first_operand(opcode)) */
	/* if(opcode == VOP_STORE) */
	/* { */
	/* 	//when using store save the indirect operand and set it as key and overwrite the value with a immediate value or register */
	/* 	assert(ctx->function); */
	/* 	vopcache_t *fnd = find_vopcache_by_key(ctx, &a); */
	/* 	if(!fnd) */
	/* 	{ */
	/* 		fnd = &ctx->function->vopcache[ctx->function->vopcacheindex]; */
	/* 		ctx->function->vopcacheindex = (ctx->function->vopcacheindex + 1) % VOPCACHE_MAX; */
	/* 	} */
	/* 	fnd->key = a; */
	/* 	fnd->value = b; */
	/* } */
	return instr;
}

static vinstr_t* emit_instruction3(compiler_t* ctx, vopcode_t opcode, voperand_t a, voperand_t b, voperand_t c)
{
	vinstr_t* instr = emit_instruction(ctx, opcode);
	set_operand(instr, 0, a);
	set_operand(instr, 1, b);
	set_operand(instr, 2, c);
	instr->numoperands = 3;
	return instr;
}

function_t *compiler_alloc_function(compiler_t *ctx, const char *name)
{
	function_t gv;
	gv.vopcacheindex = 0;
	gv.index = ctx->numfunctions++;
	/* gv.numreturns = 0; */
	snprintf(gv.name, sizeof(gv.name), "%s", name);
	gv.localvariablesize = 0;
	//TODO: free/cleanup variables
	gv.variables = hash_map_create_with_custom_allocator(variable_t, ctx->allocator, arena_alloc);
	gv.arguments = hash_map_create_with_custom_allocator(variable_t, ctx->allocator, arena_alloc);
	gv.bytecode = NULL;
	gv.instructions = arena_alloc(ctx->allocator, sizeof(vinstr_t) * FUNCTION_MAX_INSTRUCTIONS);
	gv.instruction_index = 0;
	hash_map_insert(ctx->functions, name, gv);
	
	//TODO: FIXME make insert return a reference to the data inserted instead of having to find it again.
	return hash_map_find(ctx->functions, name);
}

void compiler_init(compiler_t *c, arena_t *allocator, int numbits, compiler_flags_t flags)
{
	assert(numbits == 64);
	
	memset(c, 0, sizeof(compiler_t));

	c->flags = flags;

	//x64
	c->fts.longsize = 64;
	c->fts.intsize = 32;
	c->fts.shortsize = 16;
	c->fts.charsize = 8;
	c->fts.floatsize = 32;
	c->fts.doublesize = 64;
	c->fts.pointersize = 64;
	
	c->vregindex = VREG_MAX;
	c->labelindex = 0;
	
	c->numbits = numbits;
	c->allocator = allocator;
	c->numfunctions = 0;
	c->functions = hash_map_create_with_custom_allocator(function_t, c->allocator, arena_alloc);

	// mainly just holder for global variables and maybe code without function
	c->function = compiler_alloc_function(c, "_global_variables");
}

function_t* lookup_function_by_name(compiler_t* ctx, const char* name)
{
	hash_map_foreach_entry(ctx->functions, it, {
		if (!strcmp(it->key, name))
			return it->data;
	});
	return NULL;
}

static int fundamental_type_size(compiler_t* ctx, int type)
{
	switch (type)
	{
		case DT_CHAR:
			return ctx->fts.charsize;
		case DT_SHORT:
			return ctx->fts.shortsize;
		case DT_INT:
			return ctx->fts.intsize;
		case DT_LONG:
			return ctx->fts.longsize;
		case DT_FLOAT:
			return ctx->fts.floatsize;
		case DT_DOUBLE:
			return ctx->fts.doublesize;
		case DT_VOID:
			return 0;
	}
	debug_printf("unhandled type %d\n", type);
	return 0;
}

static variable_t *find_variable(compiler_t *ctx, const char *varname)
{
	// try local variables first
	variable_t *var = hash_map_find(ctx->function->variables, varname);
	if(var)
		return var;
	// try arguments
	var = hash_map_find(ctx->function->arguments, varname);
	if(var)
		return var;
	//TODO: globals
	return NULL;
}

static int data_type_size(compiler_t* ctx, ast_node_t* n)
{
	switch (n->type)
	{
		case AST_FUNCTION_CALL_EXPR:
		{
			ast_node_t* callee = n->call_expr_data.callee;
			function_t* fn = lookup_function_by_name(ctx, callee->identifier_data.name);
			assert(fn);
			return fn->returnsize;
		}
		break;
		
		case AST_LITERAL:
		{
			ast_literal_t *lit = &n->literal_data;
			if(lit->integer.suffix == INTEGER_SUFFIX_LONG_LONG || lit->integer.suffix == INTEGER_SUFFIX_LONG)
			{
				return ctx->fts.longsize;
			}
			return ctx->fts.intsize;
		}
		break;
		
		case AST_STRUCT_DATA_TYPE:
		{
			int total = 0;
			ast_node_t* ref = n->data_type_data.data_type;
			assert(ref);
			if (ref->type == AST_UNION_DECL)
			{
				for (int i = 0; i < ref->struct_decl_data.numfields; ++i)
				{
					int tmp = data_type_size(ctx, ref->struct_decl_data.fields[i]->variable_decl_data.data_type);
					if (tmp > total)
						total = tmp;
				}
			}
			else if (ref->type == AST_STRUCT_DECL)
			{
				for (int i = 0; i < ref->struct_decl_data.numfields; ++i)
					total += data_type_size(ctx, ref->struct_decl_data.fields[i]->variable_decl_data.data_type);
			}
			else
			{
				/* debug_printf("unhandled struct data type node '%s', can't get size\n", */
				/* 			 AST_NODE_TYPE_to_string(ref->type)); */
			}
			return total;
		}
		break;
		case AST_IDENTIFIER:
		{
			variable_t* var = find_variable(ctx, n->identifier_data.name);
			assert(var);
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
			assert(n->data_type_data.array_size > 0);

			if (n->data_type_data.data_type->type == AST_ARRAY_DATA_TYPE)
				return data_type_size(ctx, n->data_type_data.data_type) * n->data_type_data.array_size;
			else if (n->data_type_data.data_type->type == AST_PRIMITIVE)
			{
				// printf("array size = %d, primitive_type_size = %d\n", n->data_type_data.array_size,
				// primitive_data_type_size(  n->data_type_data.data_type->primitive_data_type_data.primitive_type ));
				return fundamental_type_size(ctx, n->data_type_data.data_type->primitive_data.primitive_type) *
					   n->data_type_data.array_size;
			}
			else
			{
				perror("unhandled else for data_type_size\n");
				return 0;
			}
		}
		break;
	}
	/* debug_printf("unhandled data type node '%s', can't get size\n", AST_NODE_TYPE_to_string(n->type)); */
	return 0;
}

static bool voperand_is_floating_point(voperand_t* o)
{
	return o->size == VOPERAND_SIZE_DOUBLE ||
		   o->size == VOPERAND_SIZE_FLOAT; // || o->type == VOPERAND_REGISTER && o->reg.usage == VRU_FLOATING_POINT;
}

static void store_operand(compiler_t *ctx, voperand_t *dst, voperand_t *src)
{
	switch(dst->type)
	{
		case VOPERAND_INDIRECT_REGISTER:
		case VOPERAND_INDIRECT_REGISTER_DISPLACEMENT:
		case VOPERAND_INDIRECT_REGISTER_INDEXED:
		case VOPERAND_INDIRECT:
			if (src->size == dst->size)
			{
				/* emit_instruction2(ctx, VOP_STORE, *dst, *src); */
				emit_instruction2(ctx, VOP_MOV, *dst, *src);
			}
			else
			{
				bool fa = voperand_is_floating_point(dst);
				bool fb = voperand_is_floating_point(src);

				if(!fa && !fb)
				{
					//TODO: handle native to 8/16/32/64 bits
					emit_instruction2(ctx, VOP_MOV, *dst, *src);
					/* emit_instruction2(ctx, VOP_STORE, *dst, *src); */
				} else
				{

					// making a whole of assumptions here... hmm
					// TODO: FIXME
					voperand_t iop = register_operand(get_vreg(ctx));
					iop.size = dst->size;
					emit_instruction2(ctx, VOP_FPTOSI, iop, *src);
					emit_instruction2(ctx, VOP_MOV, *dst, iop);
					/* emit_instruction2(ctx, VOP_STORE, *dst, iop); */
					*dst = iop;
				}
			}
			break;
		default:
			perror("unhandled store operand type");
			break;
	}
}

static vinstr_t *previous_instruction(compiler_t *ctx)
{
	assert(ctx->function);
	if(ctx->function->instruction_index == 0)
		return NULL;
	return &ctx->function->instructions[ctx->function->instruction_index - 1];
}

static void try_reuse_operand(compiler_t* ctx, voperand_t* op)
{
	/* assert(ctx->function); */
	/* for (size_t i = 0; i < ctx->function->vopcacheindex; ++i) */
	/* { */
	/* 	/\* print_instruction_operand(&ctx->function->vopcache[i].key, false); *\/ */
	/* 	/\* printf("\t\t\t"); *\/ */
	/* 	/\* print_instruction_operand(&ctx->function->vopcache[i].value, false); *\/ */
	/* 	/\* printf("\t\t\t"); *\/ */
	/* 	/\* print_instruction_operand(op, false); *\/ */
	/* 	/\* printf("\n"); *\/ */
	/* 	if (!memcmp(&ctx->function->vopcache[i].key, op, sizeof(voperand_t))) */
	/* 	{ */
	/* 		*op = ctx->function->vopcache[i].value; */
	/* 		break; */
	/* 	} */
	/* } */
}

static void load_operand(compiler_t* ctx, voperand_t* dst, voperand_t* src)
{
	try_reuse_operand(ctx, src);
	
	assert(dst->type == VOPERAND_REGISTER);
	if (dst->reg.index >= VREG_MAX)
		dst->size = src->size;
	
	switch (src->type)
	{
		case VOPERAND_REGISTER:
			if (dst->size != src->size && dst->reg.index != src->reg.index)
				emit_instruction2(ctx, VOP_MOV, *dst, *src);
			else
				*dst = *src;
			break;

		case VOPERAND_IMMEDIATE:
			emit_instruction2(ctx, VOP_MOV, *dst, *src);
			break;

		case VOPERAND_INDIRECT_REGISTER_DISPLACEMENT:
		case VOPERAND_INDIRECT:
		case VOPERAND_INDIRECT_REGISTER_INDEXED:
		case VOPERAND_INDIRECT_REGISTER:
		{
			//commented out should be fixed by vopcache
			/* vinstr_t* prev = previous_instruction(ctx); */
			/* if (prev && prev->opcode == VOP_STORE) */
			/* { */
			/* 	voperand_t* prevsrc = &prev->operands[1]; */
			/* 	if(voperand_equal(prevsrc, dst)) */
			/* 	{ */
			/* 		*dst = prev->operands[0]; */
			/* 		return; */
			/* 	} */
			/* } */
			emit_instruction2(ctx, VOP_MOV, *dst, *src);
			/* emit_instruction2(ctx, VOP_LOAD, *dst, *src); */
		}
		break;

		default:
			perror("unhandled load operand type");
			break;
	}
}

bool lvalue( compiler_t* ctx, ast_node_t* n, voperand_t *dst )
{
	switch ( n->type )
	{
		case AST_IDENTIFIER:
		{
			variable_t* var = find_variable(ctx, n->identifier_data.name);
			assert(var);
			ast_node_t* variable_type = var->data_type_node;
			int numbytes = data_type_size(ctx, variable_type) / 8;
			vregister_t bpreg = {.index = VREG_BP};
#define INDIRECT_ADDRESSING
#ifndef INDIRECT_ADDRESSING
			vregister_t tmpr = get_vreg(ctx);
			voperand_t tmp = register_operand(tmpr);
			emit_instruction2(ctx, VOP_MOV, tmp, register_operand(bpreg));
			emit_instruction2(ctx, VOP_ADD, tmp, imm32_operand(var->offset));
			voperand_t src = indirect_register_operand(tmpr);
#else
			voperand_t src = indirect_register_displacement_operand(bpreg, var->offset, numbytes);
#endif
			if (variable_type->type == AST_PRIMITIVE)
			{
					/* *dst = register_operand(get_vreg_with_usage(ctx, VRU_FLOATING_POINT)); */
					/* emit_instruction2(ctx, VOP_SITOFP, *dst, src); */
					if (variable_type->primitive_data.primitive_type == DT_DOUBLE)
						src.size = VOPERAND_SIZE_DOUBLE;
					if (variable_type->primitive_data.primitive_type == DT_FLOAT)
						src.size = VOPERAND_SIZE_FLOAT;
			}

			// check whether we already referenced this before
			/* for (size_t i = 0; i < ctx->function->instruction_index; ++i) */
			/* { */
			/* 	vinstr_t* instr = &ctx->function->instructions[i]; */
			/* 	if (instr->opcode != VOP_LEA) */
			/* 		continue; */
			/* 	if (memcmp(&instr->operands[1], &src, sizeof(voperand_t))) */
			/* 		continue; */
			/* 	*dst = instr->operands[0]; */
			/* 	return true; */
			/* } */

			/* *dst = indirect_register_operand(get_vreg(ctx)); */
			/* emit_instruction2(ctx, VOP_LEA, *dst, src); */
			*dst = src;
		} break;

		default:
			printf("unhandled node type %s\n", ast_node_type_t_to_string(n->type));
			return false;
	}
	return true;
}

static int deduce_literal_type(int a, int b)
{
	//if they're both the same, return the literal type
	//e.g LITERAL_INT and LITERAL_INT
	if(a == b)
		return a;

	//TODO: FIXME LITERAL_STRING
	//casting pointer to int/flt/dbl and vice versa
	
	//double > float > long long > long > short > char
	static int highest = -1;
	static int order[] = {LITERAL_NUMBER,LITERAL_INTEGER,LITERAL_STRING};
	for(int i = 0; i < COUNT_OF(order); ++i)
	{
		if(order[i] == a || order[i] == b)
		{
			highest = order[i];
			break;
		}
	}
	assert(highest != -1);
	return highest;
}

typedef struct
{
	int node_type;
	void (*callback)(compiler_t*, ast_node_t*, voperand_t*);
} rvalue_map_t;

void literal(compiler_t* ctx, ast_node_t* n, voperand_t* dst)
{
	ast_literal_t *lit = &n->literal_data;
	switch (lit->type)
	{
		case LITERAL_INTEGER:
		{
			voperand_t op = {.type = VOPERAND_IMMEDIATE};
			op.imm.is_unsigned = lit->integer.is_unsigned;
			op.imm.nbits = data_type_size(ctx, n);
			if(op.imm.nbits == 64)
				op.imm.dq = lit->integer.value;
			else
			{
				//TODO: FIXME unsigned etc conversions
				op.imm.dw = (i32)lit->integer.value;
			}
			*dst = op;
		}
		break;

	case LITERAL_NUMBER:
	{
		i64 as_int;
		double as_double = (double)lit->scalar.value;
		assert(sizeof(double) == sizeof(as_int));
		memcpy(&as_int, &as_double, sizeof(as_int));
		*dst = imm64_operand(as_int);
		dst->size = VOPERAND_SIZE_DOUBLE;
		/* *dst = register_operand(get_vreg_with_usage(ctx, VRU_FLOATING_POINT)); */
		/* emit_instruction2(ctx, VOP_SITOFP, *dst, imm64_operand(as_int)); */
	}
	break;

	default:
		perror("unhandled literal");
		break;
	}
}

static size_t instruction_index(compiler_t* ctx)
{
	assert(ctx->function);
	return ctx->function->instruction_index - 1;
}

void bin_expr(compiler_t* ctx, ast_node_t* n, voperand_t* dst)
{
	voperand_t lhs, rhs;
	rvalue(ctx, n->bin_expr_data.lhs, &lhs);
	rvalue(ctx, n->bin_expr_data.rhs, &rhs);
	switch (n->bin_expr_data.operator)
	{
		case '+':
		case '-':
		case '/':
		case '*':
		case '%':
		case '^':
		case '&':
		case '|':
		case '!':
		{
			static int opcode_map[] = {
				['+'] = VOP_ADD, ['-'] = VOP_SUB, ['/'] = VOP_DIV, ['*'] = VOP_MUL, ['%'] = VOP_MOD,
				['^'] = VOP_XOR, ['&'] = VOP_AND, ['|'] = VOP_OR,  ['!'] = VOP_NOT};
			static int opcode_map_flt[] = {
				['+'] = VOP_FADD, ['-'] = VOP_FSUB, ['/'] = VOP_FDIV, ['*'] = VOP_FMUL, ['%'] = VOP_FMOD
			};
			bool fa = voperand_is_floating_point(&lhs);
			bool fb = voperand_is_floating_point(&rhs);
			if(fa || fb)
			{
				voperand_t fop = register_operand(get_vreg(ctx));
				fop.size = VOPERAND_SIZE_DOUBLE;
				if(!fa)
				{
					emit_instruction2(ctx, VOP_SITOFP, fop, lhs);
					lhs = fop;
				}
				if(!fb)
				{
					emit_instruction2(ctx, VOP_SITOFP, fop, rhs);
					rhs = fop;
				}
				
				*dst = register_operand(get_vreg(ctx));
				dst->size = VOPERAND_SIZE_DOUBLE;
				/* *dst = register_operand(get_vreg_with_usage(ctx, VRU_FLOATING_POINT)); */

				if (ctx->flags & COMPILER_FLAGS_ALU_THREE_OPERANDS)
				{
					emit_instruction3(ctx, opcode_map_flt[n->bin_expr_data.operator], *dst, lhs, rhs);
				} else
				{
					load_operand(ctx, dst, &lhs);
					emit_instruction2(ctx, opcode_map_flt[n->bin_expr_data.operator], *dst, rhs);
				}
			}
			else
			{
				if(ctx->flags & COMPILER_FLAGS_ALU_THREE_OPERANDS)
				{
					*dst = register_operand(get_vreg(ctx));
					emit_instruction3(ctx, opcode_map[n->bin_expr_data.operator], *dst, lhs, rhs);
				} else
				{
					*dst = register_operand(get_vreg(ctx));
					load_operand(ctx, dst, &lhs);
					emit_instruction2(ctx, opcode_map[n->bin_expr_data.operator], *dst, rhs);
				}
			}
		}
		break;

		case '>':
		case '<':
		case TK_LEQUAL:
		case TK_GEQUAL:
		case TK_EQUAL:
		case TK_NOT_EQUAL:
		{
			static int opcode_map[] = {
				['>'] = VOP_JG,
				['<'] = VOP_JL,
				[TK_LEQUAL] = VOP_JLE,
				[TK_GEQUAL] = VOP_JGE,
				[TK_EQUAL] = VOP_JNZ,
				[TK_NOT_EQUAL] = VOP_JZ
			};
			emit_instruction2(ctx, VOP_CMP, lhs, rhs);
			voperand_t regop = register_operand(get_vreg(ctx));
			emit_instruction2(ctx, VOP_MOV, regop, imm32_operand(1));

			vinstr_t* jle = emit_instruction(ctx, opcode_map[n->bin_expr_data.operator]);
			voperand_t jle_label = label_operand(get_label(ctx));
			
			set_operand(jle, 0, jle_label);
			emit_instruction2(ctx, VOP_MOV, regop, imm32_operand(0));
			/* set_operand(jle, 0, imm32_operand(instruction_index(ctx) - jle->index)); */
			emit_instruction1(ctx, VOP_LABEL, jle_label);
			*dst = regop;
		} break;

		default:
		{
			perror("unhandled operator");
		}
		break;
	}
}

void assignment_expr(compiler_t* ctx, ast_node_t* n, voperand_t* dst)
{
	voperand_t lhs, rhs;
	lvalue(ctx, n->assignment_expr_data.lhs, &lhs);
	rvalue(ctx, n->assignment_expr_data.rhs, &rhs);
	store_operand(ctx, &lhs, &rhs);
	/* printf("store %s, %s\n", operand_to_string(lhs), operand_to_string(rhs)); */
	*dst = rhs;
}

void function_call_expr(compiler_t* ctx, ast_node_t* n, voperand_t* dst)
{
	ast_node_t** args = n->call_expr_data.arguments;
	int numargs = n->call_expr_data.numargs;
	ast_node_t* callee = n->call_expr_data.callee;
	function_t* fn = lookup_function_by_name(ctx, callee->identifier_data.name);

	size_t cost = 0;

	for(size_t i = 0; i < numargs; ++i)
	{
		ast_node_t* arg = args[numargs - i - 1];
		assert(arg->type == AST_IDENTIFIER || arg->type == AST_PRIMITIVE || arg->type == AST_LITERAL || arg->type == AST_FUNCTION_CALL_EXPR);
		int vs = data_type_size(ctx, arg) / 8;
		assert(vs > 0);
		voperand_t op;
		rvalue(ctx, arg, &op);
		emit_instruction1(ctx, VOP_PUSH, op);
		cost += vs;
	}

	assert(cost == fn->argcost);

	emit_instruction1(ctx, VOP_CALL, imm32_operand(fn->index));
	vregister_t spreg = {.index = VREG_SP};
	emit_instruction2(ctx, VOP_ADD, register_operand(spreg), imm32_operand(fn->argcost));
	vregister_t retreg = {.index = VREG_RETURN_VALUE };
	*dst = register_operand(retreg);
	/* if(!fn) */
	/* { */
	/* 	printf("can't find function '%s'\n", callee->identifier_data.name); */
	/* 	return; */
	/* } */
	/* emit_instruction1(ctx, VOP_CALL, imm32_operand(fn->index)); */
}

rvalue_map_t rvalues[] = {{AST_LITERAL, literal},
						  {AST_BIN_EXPR, bin_expr},
						  {AST_ASSIGNMENT_EXPR, assignment_expr},
						  {AST_FUNCTION_CALL_EXPR, function_call_expr}};

bool rvalue(compiler_t* ctx, ast_node_t* n, voperand_t* dst)
{
	for(size_t i = 0; i < COUNT_OF(rvalues); ++i)
	{
		if(rvalues[i].node_type == n->type)
		{
			rvalues[i].callback(ctx, n, dst);
			try_reuse_operand(ctx, dst);
			return true;
		}
	}
	voperand_t lhs;
	if (!lvalue(ctx, n, &lhs))
	{
		printf("unhandled rvalue '%s'\n", ast_node_type_t_to_string(n->type));
		return false;
	}
	*dst = register_operand(get_vreg(ctx));
	load_operand(ctx, dst, &lhs);
	/* emit_instruction2(ctx, VOP_MOV, *dst, lhs); */
	/* printf("load %s, %s\n", operand_to_string(*dst), operand_to_string(lhs)); */
	return true;
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

static ast_node_t *allocate_variable(compiler_t *ctx, ast_node_t *n, const char *varname, int offset, int size)
{
	variable_t tv = { .offset = offset, .is_param = 0, .data_type_node = n->variable_decl_data.data_type };
	hash_map_insert( ctx->function->variables, varname, tv );
}

typedef struct
{
	int node_type;
	bool (*callback)(compiler_t*, ast_node_t*);
} ast_node_map_t;

bool program(compiler_t* ctx, ast_node_t* n)
{
	linked_list_reversed_foreach(n->program_data.body, ast_node_t**, it, { compile_visit_node(ctx, (*it)); });
}

bool return_statement(compiler_t* ctx, ast_node_t* n)
{
	voperand_t op;
	rvalue(ctx, n->return_stmt_data.argument, &op);

	vregister_t retreg = {.index = VREG_RETURN_VALUE };
	voperand_t retop = register_operand(retreg);
	load_operand(ctx, &retop, &op);
	emit_instruction1(ctx, VOP_JMP, ctx->function->eoflabel);
	/* ctx->function->returns[ctx->function->numreturns++] = emit_instruction1(ctx, VOP_JMP, imm32_operand(0)); */
	return true;
}
bool block_statement(compiler_t* ctx, ast_node_t* n)
{
	// TODO: stack of scopes
	linked_list_reversed_foreach(n->block_stmt_data.body, ast_node_t**, it, { compile_visit_node(ctx, *it); });
	return true;
}
bool function_declaration(compiler_t* ctx, ast_node_t* n)
{
	if (n->func_decl_data.body)
	{
		const char* function_name = n->func_decl_data.id->identifier_data.name;
		function_t* oldfunc = ctx->function;
		function_t* tmp = hash_map_find(ctx->functions, function_name);
		compiler_assert(ctx, !tmp, "function already exists '%s'", function_name);
		function_t* func = compiler_alloc_function(ctx, function_name);
		ctx->function = func;
		func->eoflabel = label_operand(get_label(ctx));
		func->returnsize = data_type_size(ctx, n->func_decl_data.return_data_type);
		
		emit_instruction(ctx, VOP_ENTER);
		/* vregister_t bpreg = {.index = VREG_BP, .usage = VRU_GENERAL_PURPOSE}; */
		/* vregister_t spreg = {.index = VREG_SP, .usage = VRU_GENERAL_PURPOSE}; */
		/* emit_instruction1(ctx, VOP_PUSH, register_operand(bpreg)); */
		/* emit_instruction2(ctx, VOP_MOV, register_operand(bpreg), register_operand(spreg)); */

		// TODO: FIXME can't add any new functions to the functions hash map or otherwise ctx->function gets invalidated
		// maybe store function name instead

		// TODO: FIXME

		ast_function_decl_t* fd = &n->func_decl_data;
		ast_node_t **parms = fd->parameters;

		int poffset = 8;
		int numbytesarg = 0;
		
		for(size_t i = 0; i < fd->numparms; ++i)
		{
			const char* variable_name = parms[i]->variable_decl_data.id->identifier_data.name;
			variable_t* tmp = hash_map_find(ctx->function->arguments, variable_name);
			compiler_assert(ctx, !tmp, "function argument already exists '%s'", variable_name);
			
			variable_t tv = {.offset = poffset, .is_param = 1, .data_type_node = parms[i]->variable_decl_data.data_type};
			hash_map_insert(ctx->function->arguments, variable_name, tv);
			int variable_size = data_type_size(ctx, parms[i]->variable_decl_data.data_type);
			poffset += variable_size / 8;
			numbytesarg += variable_size / 8;
			//TODO: handle pass by value with rep movsd etc, or just use only pointers for now
		}

		func->argcost = numbytesarg;

		// add up all the variables in the function's scope and calculate how much space in bytes we need to allocate
		/* traverse_context_t traverse_ctx = {0}; */

		/* ast_node_t* variable_declarations[32]; */
		/* size_t num_variable_declarations = */
		/* 	ast_tree_nodes_by_type(&traverse_ctx, n->func_decl_data.body, AST_VARIABLE_DECL, &variable_declarations, */
		/* 						   COUNT_OF(variable_declarations)); */
		// printf("vars %d\n", num_variable_declarations);
		int offset = -4;
		size_t numbytes = 0;
		ast_node_t **declarations = fd->declarations;
		for (size_t i = 0; i < fd->numdeclarations; ++i)
		{
			const char* variable_name = declarations[i]->variable_decl_data.id->identifier_data.name;
			variable_t* tmp = hash_map_find(ctx->function->variables, variable_name);
			compiler_assert(ctx, !tmp, "variable already exists '%s'", variable_name);
			int variable_size = data_type_size(ctx, declarations[i]->variable_decl_data.data_type);
			size_t nb = variable_size / 8;
			allocate_variable(ctx, declarations[i], variable_name, offset, nb);
			/* printf("var name = %s %s, offset = %d, numbits=%d\n", ast_node_type_t_to_string(declarations[i]->type), variable_name, offset, variable_size / 8); */
			offset -= nb;
			numbytes += nb;
		}

		vregister_t bpreg = {.index = VREG_BP};
		// allocate space for local variables

		/* // align to 16 bytes */
		/* numbytes += 16 - (numbytes % 16); */

		/* emit_instruction2(ctx, VOP_SUB, register_operand(bpreg), imm32_operand(numbytes)); */

		emit_instruction1(ctx, VOP_ALLOCA, imm32_operand(numbytes));

		compile_visit_node(ctx, n->func_decl_data.body);

		/* for (size_t i = 0; i < func->numreturns; ++i) */
		/* { */
		/* 	set_operand(func->returns[i], 0, imm32_operand(instruction_index(ctx) - func->returns[i]->index)); */
		/* } */
		emit_instruction1(ctx, VOP_LABEL, ctx->function->eoflabel);
		emit_instruction(ctx, VOP_LEAVE);
		/* emit_instruction2(ctx, VOP_MOV, register_operand(spreg), register_operand(bpreg)); */
		/* emit_instruction1(ctx, VOP_POP, register_operand(bpreg)); */
		emit_instruction(ctx, VOP_RET);

		ctx->function = oldfunc;
	}
	else
	{
		perror("todo implement");
		return false;
	}
	return true;
}
bool variable_declaration(compiler_t* ctx, ast_node_t* n)
{
	ast_node_t* id = n->variable_decl_data.id;
	ast_node_t* iv = n->variable_decl_data.initializer_value;
	if (iv)
	{
		ast_node_t c = {.type = AST_ASSIGNMENT_EXPR};
		c.assignment_expr_data.lhs = id;
		c.assignment_expr_data.rhs = iv;
		c.assignment_expr_data.operator= '=';
		compile_visit_node(ctx, &c);
	}
	return true;
}

static scope_t* active_scope(compiler_t* ctx)
{
	if (ctx->scope_index == 0)
		return NULL;
	return ctx->scope[ctx->scope_index - 1];
}

static void enter_scope(compiler_t* ctx, scope_t *scope)
{
	assert(ctx->scope_index + 1 < COUNT_OF(ctx->scope));
	/* scope->numbreaks = 0; */
	/* scope->maxbreaks = COUNT_OF(scope->breaks); */
	scope->breaklabel = invalid_operand();
	ctx->scope[ctx->scope_index++] = scope;
}

static void exit_scope(compiler_t* ctx)
{
	--ctx->scope_index;
	ctx->scope[ctx->scope_index] = NULL;
}

bool break_statement(compiler_t* ctx, ast_node_t* n)
{
	scope_t* scope = active_scope(ctx);
	assert(scope);
	/* vinstr_t* jmp = emit_instruction1(ctx, VOP_JMP, imm32_operand(0)); // set temporarily to 0 */
	/* scope->breaks[scope->numbreaks++] = jmp; */
	emit_instruction1(ctx, VOP_JMP, scope->breaklabel);
	return true;
}

static void actualize_relative_scope_jumps(compiler_t *ctx)
{
	/* scope_t* scope = active_scope(ctx); */
	/* assert(scope); */

	/* size_t current_instruction_index = instruction_index(ctx); */

	/* for (size_t i = 0; i < scope->numbreaks; ++i) */
	/* { */
	/* 	vinstr_t *jmp = scope->breaks[i]; */
	/* 	set_operand(jmp, 0, imm32_operand(current_instruction_index - jmp->index));	 */
	/* } */
}

bool if_statement(compiler_t* ctx, ast_node_t* n)
{
	voperand_t op;
	rvalue(ctx, n->if_stmt_data.test, &op);

	emit_instruction2(ctx, VOP_TEST, op, op);
	voperand_t jz_label = label_operand(get_label(ctx));
	vinstr_t* jz = emit_instruction1(ctx, VOP_JZ, jz_label);
	/* vinstr_t* jz = emit_instruction1(ctx, VOP_JZ, imm32_operand(0)); */
	assert(n->if_stmt_data.consequent);
	compile_visit_node(ctx, n->if_stmt_data.consequent);

	voperand_t skip_if_label = label_operand(get_label(ctx));
	
	vinstr_t* jmp = NULL;
	if (n->if_stmt_data.alternative)
	{
		jmp = emit_instruction1(ctx, VOP_JMP, skip_if_label);
		/* jmp = emit_instruction1(ctx, VOP_JMP, imm32_operand(0)); */
	}

	emit_instruction1(ctx, VOP_LABEL, jz_label);
	/* set_operand(jz, 0, imm32_operand(instruction_index(ctx) - jz->index)); */
	
	if (n->if_stmt_data.alternative)
	{
		voperand_t jnz_label = label_operand(get_label(ctx));
		vinstr_t* jnz = emit_instruction1(ctx, VOP_JNZ, jnz_label);
		/* vinstr_t* jnz = emit_instruction1(ctx, VOP_JNZ, imm32_operand(0)); */
		compile_visit_node(ctx, n->if_stmt_data.alternative);
		emit_instruction1(ctx, VOP_LABEL, jnz_label);
		/* set_operand(jnz, 0, imm32_operand(instruction_index(ctx) - jnz->index)); */
	}

	if(jmp)
	{
		/* set_operand(jmp, 0, imm32_operand(instruction_index(ctx) - jmp->index)); */
		emit_instruction1(ctx, VOP_LABEL, skip_if_label);
	}
	return true;
}

bool while_statement(compiler_t* ctx, ast_node_t* n)
{
	scope_t scope = {0};
	enter_scope(ctx, &scope);

	size_t pos_beg = instruction_index(ctx);

	voperand_t beginlabel = label_operand(get_label(ctx));
	scope.breaklabel = label_operand(get_label(ctx));
	emit_instruction1(ctx, VOP_LABEL, beginlabel);

	voperand_t op;
	rvalue(ctx, n->while_stmt_data.test, &op);
	
	emit_instruction2(ctx, VOP_TEST, op, op);
	/* vinstr_t *jz = emit_instruction1(ctx, VOP_JZ, imm32_operand(0)); //set temporarily to 0 */
	emit_instruction1(ctx, VOP_JZ, scope.breaklabel);
	
	compile_visit_node(ctx, n->while_stmt_data.body);

	/* vinstr_t* jmp = emit_instruction1(ctx, VOP_JMP, imm32_operand(0)); */
	/* set_operand(jmp, 0, imm32_operand(pos_beg - jmp->index)); //jump back to beginning */
	emit_instruction1(ctx, VOP_JMP, beginlabel);

	emit_instruction1(ctx, VOP_LABEL, scope.breaklabel);
	/* size_t pos_end = instruction_index(ctx); */
	/* set_operand(jz, 0, imm32_operand(pos_end - jz->index)); //set to jump to end when test fails */
	
	/* actualize_relative_scope_jumps(ctx); */

	exit_scope(ctx);
	return true;
}

static ast_node_map_t nodes[] = {{AST_PROGRAM, program},
								 {AST_RETURN_STMT, return_statement},
								 {AST_BLOCK_STMT, block_statement},
								 {AST_FUNCTION_DECL, function_declaration},
								 {AST_VARIABLE_DECL, variable_declaration},
								 {AST_WHILE_STMT, while_statement},
								 {AST_IF_STMT, if_statement}};

bool compile_visit_node(compiler_t* ctx, ast_node_t* n)
{
	for (size_t i = 0; i < COUNT_OF(nodes); ++i)
	{
		if (nodes[i].node_type == n->type)
		{
			nodes[i].callback(ctx, n);
			return true;
		}
	}
	voperand_t op;
	return rvalue(ctx, n, &op);
}

static void print_hex(u8 *buf, size_t n)
{
	for (int i = 0; i < n; ++i)
	{
		printf("%02X%s", buf[i] & 0xff, i + 1 == n ? "" : " ");
	}
}

// TODO: instruction scheduling, build a dependency graph etc
// https://en.wikipedia.org/wiki/Instruction_scheduling
// Register allocation with graph coloring

static size_t register_lifetime(vinstr_t *instructions, size_t numinstructions, size_t current_index, voperand_t regop)
{
	assert(regop.type == VOPERAND_REGISTER || regop.type == VOPERAND_INDIRECT_REGISTER);
	size_t last_index = current_index;
	int regidx = regop.reg.index;
	for (size_t i = current_index + 1; i < numinstructions; ++i)
	{
		vinstr_t* instr = &instructions[i];
		for(size_t j = 0; j < instr->numoperands; ++j)
		{
			voperand_t *op = &instr->operands[j];
			switch(op->type)
			{
			case VOPERAND_REGISTER:
				if(op->reg.index == regidx/* && op->size == regop.size*/) //not needed to check for size, because the index is shared
					last_index = i;
				break;
			case VOPERAND_INDIRECT_REGISTER_INDEXED:
				if (op->reg_indirect_indexed.indexed_reg.index == regidx ||
					op->reg_indirect_indexed.indexed_reg.index == regidx)
					last_index = i;
				break;
			case VOPERAND_INDIRECT_REGISTER_DISPLACEMENT:
				if (op->reg_indirect_displacement.reg.index == regidx)
					last_index = i;
				break;
			case VOPERAND_INDIRECT_REGISTER:
				if (op->reg.index == regidx)
					last_index = i;
				break;
			}
		}
	}
	return last_index - current_index;
}

static void print_instructions(vinstr_t* instructions, size_t n)
{
	for (size_t i = 0; i < n; ++i)
	{
		vinstr_t* instr = &instructions[i];
		printf("%d: %s ", i, vopcode_names[instr->opcode]);
		for (size_t j = 0; j < instr->numoperands; ++j)
		{
			if (instr->operands[j].type == VOPERAND_REGISTER)
			{
				size_t lf = register_lifetime(instructions, n, i, instr->operands[j]);
				/* printf("lf=%d ", lf); */
			}
			print_instruction_operand(&instr->operands[j], j != instr->numoperands - 1);
		}
		printf("\n");
	}
}

typedef struct
{
	voperand_t op;
	size_t lifetime;
	size_t index_pushed_at;
} vreg_pushed_t;

typedef struct
{
	int index;
	vreg_pushed_t vreg[32];
	int vregnum;
} alloc_reg_t;

static void use_register(function_t *f, size_t instruction_index, alloc_reg_t *r, voperand_t *op)
{
	vreg_pushed_t *vp = &r->vreg[r->vregnum];
	vp->index_pushed_at = instruction_index;
	vp->lifetime = register_lifetime(f->instructions, f->instruction_index, instruction_index, *op);
	vp->op = *op;
}

static alloc_reg_t *alloc_register(alloc_reg_t *registers, size_t numregisters, function_t *f, size_t instruction_index, voperand_t *op, bool *push, int *ignore, bool first)
{

	// find whether we already assigned the register operand
	for (int k = 0; k < numregisters; ++k)
	{
		if (k == *ignore && first)
			continue;
		alloc_reg_t* r = &registers[k];
		if (r->vreg[r->vregnum].op.reg.index == op->reg.index)// && r->vreg[r->vregnum].op.size == op->size)
		{
			/* printf("reusing reg "); */
			/* print_instruction_operand(op, false); */
			/* printf(" for %d\n", k); */
			return r;
		}
	}
	if(!first)
	{
		printf("reg %d not set before ii=%d\n", op->reg.index, instruction_index);
		return NULL;
	}

	// we didn't find it, check whether we have unused real registers
	for (int k = 0; k < numregisters; ++k)
	{
		if (k == *ignore)
			continue;
		alloc_reg_t* r = &registers[k];
		if (r->vregnum == 0)
		{
			r->vregnum = 1;
			use_register(f, instruction_index, r, op);
			/* printf("unused real reg %d", k); */
			return r;
		}
	}
	if(*ignore == 0)
		return NULL;
	// find the register with the lowest push count
	alloc_reg_t* lowest = &registers[0];
	for (int k = 1; k < numregisters; ++k)
	{
		alloc_reg_t *r = &registers[k];
		if (r->vregnum < lowest->vregnum)
			lowest = r;
	}

	assert(lowest->vregnum < COUNT_OF(lowest->vreg));
	lowest->vregnum++;
	use_register(f, instruction_index, lowest, op);
	*push = true;
	/* printf("pushing %d ", 0); */
	/* print_instruction_operand(op, false); */
	/* printf("\n"); */
	return lowest;
}

int compile(compiler_t* ctx, ast_node_t *head)
{
    if(setjmp(ctx->jmp))
    {
		return 1;
    }
	compile_visit_node(ctx, head);

	//printf("functions:\n");
	hash_map_foreach_entry(ctx->functions, entry, {
		function_t* fn = entry->data;
		/* printf("--------------------------------\n\n"); */
		/* printf("%s, %d instructions\n", fn->name, fn->instruction_index); */
		/* print_function_instructions(fn); */
		/* printf("--------------------------------\n\n"); */
		if (!strcmp(fn->name, "main"))
		{
			//printf("bytecode=%d\n",heap_string_size(&fn->bytecode));
			/* print_hex(fn->bytecode, heap_string_size(&fn->bytecode)); */
		}
		/* print_instructions(fn->instructions, fn->instruction_index); */
		// printf("--------------------------\n");

		hash_map_foreach_entry(fn->variables, ventry,
		{
			//printf("\t%s\n", ventry->key);
		});
	});

	return 0;
}
