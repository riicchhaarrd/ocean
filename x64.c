//TODO: implement all opcodes we'll be using so we can keep track of the registers and their values
//TODO: replace our "real" registers with "virtual" registers

#include "token.h"
#include "rhd/linked_list.h"
#include <assert.h>
#include <stdlib.h>
#include "buffer_util.h"
#include "compile.h"
#include "codegen.h"

typedef enum
{
	//lower 8-bit registers
	AL,BL,CL,DL
	//upper 8-bit registers
	AH,BH,CH,DH
	//16-bit registers
	AX,BX,CX,DX
	
	//32-bit registers
    EAX,
    ECX,
    EDX,
    EBX,
    ESP,
    EBP,
    ESI,
    EDI,
	
	//lowermost 8-bits register
	R8B,R9B,R10B,R11B,R12B,R13B,R14B,R15B,
	//lowermost 16-bits register
	R8W,R9W,R10W,R11W,R12W,R13W,R14W,R15W,
	//lowermost 32-bits register
	R8D,R9D,R10D,R11D,R12D,R13D,R14D,R15D,
	
	//64-bit registers
	RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
	
	XMM0,XMM1,XMM2,XMM3,XMM4,XMM5,XMM6,XMM7, //SSE2
	YMM0,YMM1,YMM2,YMM3,YMM4,YMM5,YMM6,YMM7 //AVX
} x64_register_t;

typedef enum
{
	CF = 1,
	PF = 0x4,
	AF = 0x10,
	ZF = 0x40,
	SF = 0x80,
	TP = 0x100,
	IF = 0x200,
	DF = 0x400,
	OF = 0x800
} x64_flags_t;

static int reg_count_bits(x64_register_t reg)
{
	if(reg >= AL && reg <= DH)
		return 8;
	if(reg >= R8B && reg <= R15B)
		return 8;
	if(reg >= R8W && reg <= R15W)
		return 16;
	if(reg >= R8D && reg <= R15D)
		return 32;
	if(reg >= AX && reg <= DX)
		return 16;
	if(reg >= EAX && reg <= EDI)
		return 32;
	if(reg >= RAX && reg <= R15)
		return 64;
	if(reg >= XMM0 && reg <= XMM7)
		return 128;
	if(reg >= YMM0 && reg <= YMM7)
		return 256;
	perror("reg_count_bits invalid reg");
	return -1;
}

static x64_register_t lower_half_register_bits(x64_register_t reg)
{
	if(reg >= AL && reg <= DH) //8-bits is the lowest we can go
		return -1;
	if(reg >= AX && reg <= DX) //TODO: add upper_half_register_bits if we wanted to access those
		return AL + (reg - AX);
	if(reg >= EAX && reg <= EAX)
		return AX + (reg - EAX);
	if(reg >= RAX && reg <= RDI)
		return EAX + (reg - RAX);
	//TODO: add the other registers aswell
	perror("invalid register for lower_half_register_bits");
	return -1;
}

//what if we have more virtual registers than real registers
//then two virtual registers could be assigned to the same real register
//maybe add a stack and keep track of which virtual register is currently used for which register
//just like in the VREG map/unmap

static int map_vreg(vreg_t reg)
{	
	switch(reg)
	{
		case VREG_0:
		case VREG_1:
		case VREG_2:
		case VREG_3:
			return RAX + (reg - VREG_0);
		case VREG64_0:
		case VREG64_1:
		case VREG64_2:
		case VREG64_3:
			return RAX + (reg - VREG64_0);
			
		case VREG32_0:
		case VREG32_1:
		case VREG32_2:
		case VREG32_3:
			return EAX + (reg - VREG32_0);
			
		case VREG_SP:
			return RSP;
		case VREG_BP:
			return RBP;
	}
	perror("can't map vreg to x64 register");
	return -1;
}

static void nop(compiler_t *ctx)
{
	db(ctx, 0x90);
}

static void add(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void sub(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void mod(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void imul(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void idiv(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void add_imm8_to_r32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void add_imm32_to_r32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void inc(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void neg(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
//static void sub_regn_imm32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
//static void xor(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void and(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void or(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void int3(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void invoke_syscall(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void exit_instr(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
//static void push(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
//static void pop(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void load_reg(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void store_reg(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void load_regn_base_offset_imm32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void ret(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void indirect_call_imm32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void call_imm32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void call_r32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
//static void mov_r_imm32(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void mov_r_string(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
//static void mov(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void cmp(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void test(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void if_beg(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void if_else(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void if_end(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void jmp_begin(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void jmp_end(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
static void add_data(void) { printf("function '%s' is not implemented!", __FUNCTION__); }

static void mov_r_imm32(compiler_t* ctx, vreg_t vreg, i32 imm, int* data_loc)
{
	x64_register_t reg = map_vreg(vreg);
	if(reg > EDI)
	{
		//only valid for RAX - RDI
		db(ctx, 0x48);
		db(ctx, 0xc7);
		db(ctx, 0xc0 + (reg - RAX));
	} else if(reg < EAX)
	{
		//TODO: FIXME 16-bit unhandled
		perror("16-bit unhandled...");
	} else
	{
		db(ctx, 0xb8 + (reg - EAX));
	}
	dd(ctx, imm);
}

static vreg_t mov(compiler_t* ctx, vreg_t va, vreg_t vb)
{
	//a and b must be same size register
	x64_register_t a = map_vreg(va);
	x64_register_t b = map_vreg(vb);
	assert(a >= RAX && a <= RDI && b >= RAX && b <= RDI);
	db(ctx, 0x40);
	db(ctx, 0x89);
	db(ctx, 0xc0 + (b - RAX) * 8 + (a - RAX));
	return va;
}

static vreg_t sub_regn_imm32(compiler_t* ctx, vreg_t vreg, i32 imm)
{
	x64_register_t reg = map_vreg(vreg);
	if(reg >= RAX && reg <= RDI)
	{
		db(ctx, 0x40);
	}
	db(ctx, 0x81);
	if(reg >= RAX && reg <= RDI)
		db(ctx, 0xe8 + (reg - RAX));
	else
		db(ctx, 0xe8 + (reg - EAX));
	dd(ctx, imm);
}

static void push(compiler_t *ctx, vreg_t vreg)
{
	x64_register_t reg = map_vreg(vreg);
	assert(reg >= RAX && reg <= RDI);
	db(ctx, 0x50 + (reg - RAX));
}

static void pop(compiler_t *ctx, vreg_t vreg)
{
	x64_register_t reg = map_vreg(vreg);
	assert(reg >= RAX && reg <= RDI);
	db(ctx, 0x58 + (reg - RAX));
}

static vreg_t xor(compiler_t* ctx, vreg_t va, vreg_t vb)
{
	x64_register_t a = map_vreg(va);
	x64_register_t b = map_vreg(vb);
	int nb = reg_count_bits(a);
	assert(nb == reg_count_bits(b));
	switch(nb)
	{
		case 32:
			db(ctx, 0x31);
			db(ctx, 0xc0 + (b - EAX) * 8 + (a - EAX));
		break;
		case 64:
			assert(a >= RAX && a <= RDI);
			assert(b >= RAX && b <= RDI);
			db(ctx, 0x48);
			db(ctx, 0x31);
			db(ctx, 0xc0 + (b - RAX) * 8 + (a - RAX));
		break;
		default:
			perror("unhandled xor");
		break;
	}
	return va;
}
//for local variables
//not sure whether ARM or non-x86 support this, but for now just fix x86/x64

static void load_offset_from_stack_to_register(compiler_t *ctx, vreg_t vreg, int offset, int data_size)
{
	x64_register_t reg = map_vreg(vreg);
	assert(reg >= RAX && reg <= RDX);
	switch(data_size)
	{
		case 4: //mov eax also clears the upper bits for x64
		case 8:
		db(ctx, 0x48);
		db(ctx, 0x8b);
		db(ctx, 0x85 + (reg - RAX) * 8);
		dd(ctx, offset);
		break;
		
		case 2:
		//first xor, then mov [word]
		perror("unhandled data_size");
		break;
		
		default:
		perror("unhandled data_size");
		break;
	}
}

static void store_offset_from_register_to_stack(compiler_t *ctx, vreg_t reg, int offset, int data_size)
{
	x64_register_t reg = map_vreg(vreg);
	assert(reg >= RAX && reg <= RDX);
	db(ctx, 0x48);
	db(ctx, 0x89);
	db(ctx, 0x85 + (reg - RAX) * 8);
	dd(ctx, offset);
}

void codegen_x64(codegen_t *cg)
{
	cg->load_offset_from_stack_to_register = load_offset_from_stack_to_register;
	cg->store_offset_from_register_to_stack = store_offset_from_register_to_stack;
	
	cg->add = add;
	cg->sub = sub;
	cg->mod = mod;
	cg->imul = imul;
	cg->idiv = idiv;
	cg->add_imm8_to_r32 = add_imm8_to_r32;
	cg->add_imm32_to_r32 = add_imm32_to_r32;
	cg->inc = inc;
	cg->neg = neg;
	cg->sub_regn_imm32 = sub_regn_imm32;
	cg->xor = xor;
	cg->and = and;
	cg->or = or;
	cg->int3 = int3;
	cg->nop = nop;
	cg->invoke_syscall = invoke_syscall;
	cg->exit_instr = exit_instr;
	cg->push = push;
	cg->pop = pop;
	cg->load_reg = load_reg;
	cg->store_reg = store_reg;
	cg->load_regn_base_offset_imm32 = load_regn_base_offset_imm32;
	cg->ret = ret;
	cg->indirect_call_imm32 = indirect_call_imm32;
	cg->call_imm32 = call_imm32;
	cg->call_r32 = call_r32;
	cg->mov_r_imm32 = mov_r_imm32;
	cg->mov_r_string = mov_r_string;
	cg->mov = mov;
	cg->cmp = cmp;
	cg->test = test;
	cg->if_beg = if_beg;
	cg->if_else = if_else;
	cg->if_end = if_end;
	cg->jmp_begin = jmp_begin;
	cg->jmp_end = jmp_end;
	cg->add_data = add_data;
}
