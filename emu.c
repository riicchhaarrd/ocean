#include "imm.h"
#include "instruction.h"
#include "operand.h"

static const char* x64_register_strings[] = {
	"AL",	"BL",	"CL",	"DL",	"AH",	"BH",	"CH",	"DH",	"AX",	"BX",	"CX",	"DX",	"EAX",
	"ECX",	"EDX",	"EBX",	"ESP",	"EBP",	"ESI",	"EDI",	"R8B",	"R9B",	"R10B", "R11B", "R12B", "R13B",
	"R14B", "R15B", "R8W",	"R9W",	"R10W", "R11W", "R12W", "R13W", "R14W", "R15W", "R8D",	"R9D",	"R10D",
	"R11D", "R12D", "R13D", "R14D", "R15D", "RAX",	"RCX",	"RDX",	"RBX",	"RSP",	"RBP",	"RSI",	"RDI",
	"R8",	"R9",	"R10",	"R11",	"R12",	"R13",	"R14",	"R15",	"XMM0", "XMM1", "XMM2", "XMM3", "XMM4",
	"XMM5", "XMM6", "XMM7", "YMM0", "YMM1", "YMM2", "YMM3", "YMM4", "YMM5", "YMM6", "YMM7", NULL};

typedef enum
{
	AL,
	BL,
	CL,
	DL, // lower 8-bit registers
	AH,
	BH,
	CH,
	DH, // upper 8-bit registers
	AX,
	BX,
	CX,
	DX, // 16-bit registers
	EAX,
	ECX,
	EDX,
	EBX,
	ESP,
	EBP,
	ESI,
	EDI, // 32-bit registers
	R8B,
	R9B,
	R10B,
	R11B,
	R12B,
	R13B,
	R14B,
	R15B, // lowermost 8-bits register
	R8W,
	R9W,
	R10W,
	R11W,
	R12W,
	R13W,
	R14W,
	R15W, // lowermost 16-bits register
	R8D,
	R9D,
	R10D,
	R11D,
	R12D,
	R13D,
	R14D,
	R15D, // lowermost 32-bits register
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
	R15, // 64-bit registers
	XMM0,
	XMM1,
	XMM2,
	XMM3,
	XMM4,
	XMM5,
	XMM6,
	XMM7, // SSE2
	YMM0,
	YMM1,
	YMM2,
	YMM3,
	YMM4,
	YMM5,
	YMM6,
	YMM7, // AVX
	X64_REGISTER_MAX
} x64_register_t;

// list of registers that overlap due to being lower N bits
static const int x64_register_slots[][6] = {{RAX, EAX, AX, AL, AH, -1},
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
											{XMM7, YMM7, -1}};

static struct
{
	int bits;
	x64_register_t registers[17];
} x64_register_bits[] = {
	{256, {YMM0, YMM1, YMM2, YMM3, YMM4, YMM5, YMM6, YMM7, -1}},
	{128, {XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7, -1}},
	{64, {RAX, RCX, RDX, RBX, /*RSP,RBP,*/ RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15, -1}},
	{32, {EAX, ECX, EDX, EBX, /*ESP,EBP,*/ ESI, EDI, R8D, R9D, R10D, R11D, R12D, R13D, R14D, R15D, -1}},
	{16, {AX, BX, CX, DX, R8W, R9W, R10W, R11W, R12W, R13W, R14W, R15W, -1}},
	{8, {AL, BL, CL, DL, AH, BH, CH, DH, R8B, R9B, R10B, R11B, R12B, R13B, R14B, R15B, -1}}};

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

static bool operand_size_native(voperand_t *o)
{
	//in this case we're targetting x64, so either one is fine
	return o->size == VOPERAND_SIZE_NATIVE || o->size == VOPERAND_SIZE_64_BITS;
}

// instructions that default to 64-bit on long mode
// call, enter, jcc, jrcxz, jmp (near), leave, lgdt, lidt, lldt, loop, loopcc, ltr, mov cr(n), mov dr(n), pop reg/mem,
// pop reg pop fs, pop gs, popfq, push imm8, push imm32, push reg/mem, push reg, push fs, push gs, pushfq, ret (near)

// W when 1 64-bit operand size is used
// R modrm.reg field
// X sib.index field
// B modrm.rm field or sib.base field

typedef struct
{
	int W, R, X, B;
} rex_fields_t;

static u8 encode_rex_prefix(rex_fields_t rf)
{
	rf.W = (rf.W != 0);
	rf.R = (rf.R != 0);
	rf.X = (rf.X != 0);
	rf.B = (rf.B != 0);
	// 0             7
	// B X R W 0 0 1 0
	// NOTE: assuming little-endian for now
	return 2 | (rf.W << 4) | (rf.R << 5) | (rf.X << 6) | (rf.B << 7);
}

// sets rex field W if needed and returns the value of the register used in the reg field of modrm
// should be shifted to the reg position (x64 little-endian e.g << 3)
// value depends on the instruction which register is used see
// https://wiki.osdev.org/X86-64_Instruction_Encoding#Registers
static u8 encode_register_reference(x64_register_t reg,
									rex_fields_t* rf) // can return max value of 15 (3 bits + extra rex field bit) = 4
{
	if (reg >= R8 && reg <= R15)
	{
		rf->R = 1;
		return (reg - R8);
	}
	else if (reg >= RAX && reg <= RDI)
		return (reg - RAX);
	else if (reg >= EAX && reg <= EDI)
		return (reg - EAX);
	// TODO: add rest of registers
	return -1;
}

typedef struct
{
} x64_context_t;

static void mov(x64_context_t* ctx, int dst, int src)
{
	// TODO: first byte replace with 0x49
	// but just redo it all and just use bitflags
	// https://staffwww.fullcoll.edu/aclifton/cs241/lecture-instruction-format.html

	rex_fields_t rf = {0};
	rf.W = (dst >= R8 && dst <= R15);
	rf.B = 0;
	rf.X = 0;
	rf.R = (src >= R8 && src <= R15);

	/*
	  7                           0
	+---+---+---+---+---+---+---+---+
	|  mod  |    reg    |     rm    |
	+---+---+---+---+---+---+---+---+
	*/
	u8 modrm = 0;
	// for now just use disp32, which is b10

	// set first mod bit to 1
	modrm |= (1 << 7);

	// set dest reg
	modrm |= encode_register_reference(dst, &rf) << 3;

	u8 rex = encode_rex_prefix(rf);
	/* db(ctx, rex); */
	/* db(ctx, 0x8b); // 	8B 		r 						MOV 	r16/32/64 	r/m16/32/64 */
	/* db(ctx, modrm); */
	/* dd(ctx, lv->offset); */
}

static void add(voperand_t *dst, voperand_t *a, voperand_t *b)
{
	
}

void handle_instruction(int opcode, vinstr_t *instr)
{
	switch (opcode)
	{
		case VOP_ADD:
		{
			assert(instr->numoperands == 3);
			voperand_t* dst = &instr->operands[0];
			voperand_t* a = &instr->operands[1];
			voperand_t* b = &instr->operands[2];

			switch (dst->size)
			{
				case VOPERAND_SIZE_NATIVE:
				case VOPERAND_SIZE_64_BITS:
					imm_cast_int64_t(&a->imm);
					break;
			}
		}
		break;
	}
}

void gen(vinstr_t *instructions, size_t n)
{
	for(size_t i = 0; i < n; ++i)
	{
		vinstr_t* instr = &instructions[i];
		/* printf("op=%s,operands=%d\n", vopcode_names[instr->opcode], instr->numoperands); */
		handle_instruction(instr->opcode, instr);
	}
}
