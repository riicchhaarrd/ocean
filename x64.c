//TODO: implement all opcodes we'll be using so we can keep track of the registers and their values
//TODO: replace our "real" registers with "virtual" registers

#include "std.h"
#include "token.h"
#include "rhd/linked_list.h"
#include <assert.h>
#include <stdlib.h>
#include "buffer_util.h"
#include "compile.h"
#include "codegen.h"

static const char *x64_register_strings[] = {"AL","BL","CL","DL","AH","BH","CH","DH","AX","BX","CX","DX","EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI","R8B","R9B","R10B","R11B","R12B","R13B","R14B","R15B","R8W","R9W","R10W","R11W","R12W","R13W","R14W","R15W","R8D","R9D","R10D","R11D","R12D","R13D","R14D","R15D","RAX","RCX","RDX","RBX","RSP","RBP","RSI","RDI","R8","R9","R10","R11","R12","R13","R14","R15","XMM0","XMM1","XMM2","XMM3","XMM4","XMM5","XMM6","XMM7","YMM0","YMM1","YMM2","YMM3","YMM4","YMM5","YMM6","YMM7",NULL};

typedef enum
{
	AL,BL,CL,DL, //lower 8-bit registers
	AH,BH,CH,DH, //upper 8-bit registers
	AX,BX,CX,DX, //16-bit registers
	EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI, //32-bit registers
	R8B,R9B,R10B,R11B,R12B,R13B,R14B,R15B, //lowermost 8-bits register
	R8W,R9W,R10W,R11W,R12W,R13W,R14W,R15W, //lowermost 16-bits register
	R8D,R9D,R10D,R11D,R12D,R13D,R14D,R15D, //lowermost 32-bits register
	RAX,RCX,RDX,RBX,RSP,RBP,RSI,RDI,R8,R9,R10,R11,R12,R13,R14,R15, //64-bit registers
	XMM0,XMM1,XMM2,XMM3,XMM4,XMM5,XMM6,XMM7, //SSE2
	YMM0,YMM1,YMM2,YMM3,YMM4,YMM5,YMM6,YMM7, //AVX
	X64_REGISTER_MAX
} x64_register_t;

//list of registers that overlap due to being lower N bits
static const int x64_register_slots[][6] = {
	{RAX, EAX, AX, AL, AH, -1},
	{RCX, ECX, CX, CL, CH, -1},
	{RDX, EDX, DX, DL, DH, -1},
	{RBX, EBX, BX, BL, BH, -1},
	{RSP, ESP, -1},
	{RBP, EBP, -1},
	{RSI, ESI, -1},
	{RDI, EDI, -1},
	{R8, R8D, R8W, R8B, -1},
	{R9, R9D, R9W, R9B, -1},
	{R10, R10D, R10W, R10B, -1},
	{R11, R11D, R11W, R11B, -1},
	{R12, R12D, R12W, R12B, -1},
	{R13, R13D, R13W, R13B, -1},
	{R14, R14D, R14W, R14B, -1},
	{R15, R15D, R15W, R15B, -1},
	{XMM0, YMM0, -1},
	{XMM1, YMM1, -1},
	{XMM2, YMM2, -1},
	{XMM3, YMM3, -1},
	{XMM4, YMM4, -1},
	{XMM5, YMM5, -1},
	{XMM6, YMM6, -1},
	{XMM7, YMM7, -1}
};

static struct
{
	int bits;
	reg_t registers[17];
} x64_register_bits[] = {
	{256, 	{YMM0,YMM1,YMM2,YMM3,YMM4,YMM5,YMM6,YMM7,-1}},
	{128,	{XMM0,XMM1,XMM2,XMM3,XMM4,XMM5,XMM6,XMM7,-1}},
	{64, 	{RAX,RCX,RDX,RBX,/*RSP,RBP,*/RSI,RDI,R8,R9,R10,R11,R12,R13,R14,R15,-1}},
	{32, 	{EAX,ECX,EDX,EBX,/*ESP,EBP,*/ESI,EDI,R8D,R9D,R10D,R11D,R12D,R13D,R14D,R15D,-1}},
	{16, 	{AX,BX,CX,DX,R8W,R9W,R10W,R11W,R12W,R13W,R14W,R15W,-1}},
	{8, 	{AL,BL,CL,DL,AH,BH,CH,DH,R8B,R9B,R10B,R11B,R12B,R13B,R14B,R15B,-1}}
};

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

static int reg_count_bits(compiler_t *ctx, x64_register_t reg)
{
	return ctx->cg.reginfo[reg].bits;
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

static void push(compiler_t *ctx, reg_t reg)
{
	assert(reg >= RAX && reg <= RDI);
	db(ctx, 0x50 + (reg - RAX));
}

static reg_t least_used_compatible_register(compiler_t *ctx, int bits)
{
	reg_t *registers = NULL;
	for(int i = 0; i < COUNT_OF(x64_register_bits); ++i)
	{
		if(x64_register_bits[i].bits == bits)
		{
			registers = x64_register_bits[i].registers;
			break;
		}
	}
	assert(registers);
	reg_t reg = registers[0];
	for(size_t i = 1; registers[i] != -1; ++i)
	{
		reginfo_t *info = &ctx->cg.reginfo[registers[i]];
		if(info->usecount < ctx->cg.reginfo[reg].usecount)
		{
			reg = registers[i];
		}
	}
	return reg;
}

//what if we have more virtual registers than real registers
//then two virtual registers could be assigned to the same real register
//maybe add a stack and keep track of which virtual register is currently used for which register
//just like in the VREG map/unmap

static reg_t map_reg(compiler_t *ctx, vreg_t vreg)
{
	reg_t reg = -1;
	switch(vreg)
	{
		case VREG_ANY:
		case VREG64_ANY:
			reg = least_used_compatible_register(ctx, 64);
			break;
		case VREG32_ANY:
			reg = least_used_compatible_register(ctx, 32);
			break;
		case VREG16_ANY:
			reg = least_used_compatible_register(ctx, 16);
			break;
		case VREG8_ANY:
			reg = least_used_compatible_register(ctx, 8);
			break;
			
		case VREG_0:
		case VREG_1:
		case VREG_2:
		case VREG_3:
			reg = RAX + (vreg - VREG_0);
			break;
		case VREG64_0:
		case VREG64_1:
		case VREG64_2:
		case VREG64_3:
			reg = RAX + (vreg - VREG64_0);
			break;
			
		case VREG32_0:
		case VREG32_1:
		case VREG32_2:
		case VREG32_3:
			reg = EAX + (vreg - VREG32_0);
			break;
			
		case VREG_SP:
			reg = RSP;
			break;
		case VREG_BP:
			reg = RBP;
			break;
	}
	//printf("vreg=%s,reg=%d,%s\n",vreg_names[vreg],reg,x64_register_strings[reg]);
	//assert(reg >= RAX && reg <= RDI);
	if(ctx->cg.reginfo[reg].usecount > 0)
	{
		push(ctx, reg);
	}
	++ctx->cg.reginfo[reg].usecount;
	return reg;
}

static void pop(compiler_t *ctx, reg_t reg)
{
	assert(reg >= RAX && reg <= RDI);
	db(ctx, 0x58 + (reg - RAX));
}

static void unmap_reg(compiler_t *ctx, reg_t reg)
{
	if(ctx->cg.reginfo[reg].usecount > 1)
	{
		pop(ctx, reg);
		--ctx->cg.reginfo[reg].usecount;
	}
}

static void nop(compiler_t *ctx)
{
	db(ctx, 0x90);
}

//static void add(void) { printf("function '%s' is not implemented!", __FUNCTION__); }
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

static void mov_r_imm32(compiler_t* ctx, reg_t reg, i32 imm, int* data_loc)
{
	if(reg >= RAX && reg <= RDI)
	{
		db(ctx, 0x48);
		db(ctx, 0xb8 + (reg - RAX));
		dd(ctx, imm);
		dd(ctx, 0);
	} else if(reg >= R8 && reg <= R15)
	{
		db(ctx, 0x49);
		db(ctx, 0xb8 + (reg - R8));
		dd(ctx, imm);
		dd(ctx, 0);
	} else {
		perror("unhandled data_size");
	}
	#if 0
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
	#endif
}

static vreg_t mov(compiler_t* ctx, reg_t a, reg_t b)
{
	//a and b must be same size register
	assert(a >= RAX && a <= RDI && b >= RAX && b <= RDI);
	db(ctx, 0x40);
	db(ctx, 0x89);
	db(ctx, 0xc0 + (b - RAX) * 8 + (a - RAX));
	return a;
}

static vreg_t sub_regn_imm32(compiler_t* ctx, reg_t reg, i32 imm)
{
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

static vreg_t xor(compiler_t* ctx, reg_t a, reg_t b)
{
	int nb = reg_count_bits(ctx, a);
	assert(nb == reg_count_bits(ctx, b));
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
	return a;
}
//for local variables
//not sure whether ARM or non-x86 support this, but for now just fix x86/x64

static void load_offset_from_stack_to_register(compiler_t *ctx, reg_t reg, int offset, int data_size)
{
	//assert(reg >= RAX && reg <= RDI);
	switch(data_size)
	{
		case 1:
		if(reg >= RAX && reg <= RDI)
		{
			db(ctx, 0x48);
			db(ctx, 0x0f);
			db(ctx, 0xbe);
			db(ctx, 0x85 + (reg - RAX) * 8);
		} else if(reg >= R8 && reg <= R15)
		{
			db(ctx, 0x48);
			db(ctx, 0x0f);
			db(ctx, 0xbe);
			db(ctx, 0x85 + (reg - R8) * 8);
		} else {
			perror("unhandled data_size");
		}
		dd(ctx, offset);
		break;
		
		case 4:
		if(reg >= RAX && reg <= RDI)
		{
			db(ctx, 0x48);
			db(ctx, 0x63);
			db(ctx, 0x85 + (reg - RAX) * 8);
		} else if(reg >= R8 && reg <= R15)
		{
			db(ctx, 0x4c);
			db(ctx, 0x63);
			db(ctx, 0x85 + (reg - R8) * 8);
		} else {
			perror("unhandled data_size");
		}
		dd(ctx, offset);
		#if 0 //probably should use movsx/movzx here atm for now, also we need data_type aswell
		if(reg >= RAX && reg <= RDI)
		{
			db(ctx, 0x67);
			db(ctx, 0x48);
			db(ctx, 0x8b);
			db(ctx, 0x85 + (reg - RAX) * 8);
		} else if(reg >= R8 && reg <= R15)
		{
			db(ctx, 0x67);
			db(ctx, 0x4c);
			db(ctx, 0x8b);
			db(ctx, 0x85 + (reg - RAX) * 8);
		} else
			perror("unhandled data_size");
		#endif
		break;
		
		case 8:
		if(reg >= RAX && reg <= RDI)
		{
			db(ctx, 0x48);
			db(ctx, 0x8b);
			db(ctx, 0x85 + (reg - RAX) * 8);
		}
		else if(reg >= R8 && reg <= R15)
		{
			db(ctx, 0x4c);
			db(ctx, 0x8b);
			db(ctx, 0x85 + (reg - R8) * 8);
		} else
			perror("unhandled data_size");
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

static void store_offset_from_register_to_stack(compiler_t *ctx, reg_t reg, int offset, int data_size)
{
	if(reg >= RAX && reg <= RDI)
	{
		db(ctx, 0x48);
		db(ctx, 0x89);
		db(ctx, 0x85 + (reg - RAX) * 8);
	} else if(reg >= R8 && reg <= R15)
	{
		db(ctx, 0x4c);
		db(ctx, 0x89);
		db(ctx, 0x85 + (reg - R8) * 8);
	} else {
		perror("unhandled data_size");
	}
	dd(ctx, offset);
}

static int reg_is_new_64_bit(reg_t reg)
{
	return reg >= R8 && reg <= R15;
}

static int reg_is_old_64_bit(reg_t reg)
{
	return reg >= RAX && reg <= RDI;
}

static reg_t add(compiler_t* ctx, reg_t a, reg_t b)
{
	assert(reg_count_bits(ctx, a) == reg_count_bits(ctx, b));
	if(!reg_is_new_64_bit(a))
	{
		if(!reg_is_new_64_bit(b))
		{
			db(ctx, 0x48);
			db(ctx, 0x01);
			db(ctx, 0xc0 + (b - RAX) * 8 + (a - RAX));
		} else
		{
			db(ctx, 0x4c);
			db(ctx, 0x01);
			db(ctx, 0xc0 + (b - R8) * 8 + (a - RAX));
		}
	} else {
		if(!reg_is_new_64_bit(b))
		{
			db(ctx, 0x49);
			db(ctx, 0x01);
			db(ctx, 0xc0 + (b - RAX) * 8 + (a - R8));
		} else
		{
			db(ctx, 0x4d);
			db(ctx, 0x01);
			db(ctx, 0xc0 + (b - R8) * 8 + (a - R8));
		}
	}
	return a;
}

static const char *register_name(compiler_t *ctx, reg_t reg)
{
	return x64_register_strings[reg];
}

void codegen_x64(compiler_t *ctx)
{
	codegen_t *cg = &ctx->cg;
	
	cg->numreginfo = X64_REGISTER_MAX;
	cg->reginfo = arena_alloc(ctx->allocator, sizeof(reginfo_t) * cg->numreginfo);
	
	for(int i = 0; i < COUNT_OF(x64_register_slots); ++i)
	{
		//printf("slot %d:", i);
		for(int j = 0; x64_register_slots[i][j] != -1; ++j)
		{
			//printf(" %s", register_name(ctx, x64_register_slots[i][j]));
			cg->reginfo[x64_register_slots[i][j]].slot = i;
			cg->reginfo[x64_register_slots[i][j]].name = register_name(ctx, x64_register_slots[i][j]);
			cg->reginfo[x64_register_slots[i][j]].id = x64_register_slots[i][j];
		}
		//printf("\n");
	}
		
	for(int i = 0; i < COUNT_OF(x64_register_bits); ++i)
	{
		//printf("bits %d:", x64_register_bits[i].bits);
		for(int j = 0; x64_register_bits[i].registers[j] != -1; ++j)
		{
			//printf(" %s", register_name(ctx, x64_register_bits[i].registers[j]));
			cg->reginfo[x64_register_bits[i].registers[j]].bits = x64_register_bits[i].bits;
		}
		//printf("\n");
	}
	
	cg->map_register = map_reg;
	cg->unmap_register = unmap_reg;
	cg->register_name = register_name;
	
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
