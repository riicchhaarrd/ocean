#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"
#include "data_type.h"
#include "rhd/hash_string.h"

//TODO: implement later
typedef enum
{
	VREG8,
	//VREG8 + 1...
	VREG16 = 4,
	//VREG16 + 1...
	VREG32 = 8,
	//VREG32 + 1...
	VREG64 = 12,
	
	VREGSP = 16,
	VREGBP,
	VREGIP,
	VREGMAX
} vreg_t;

typedef enum
{
    EAX, //0
    ECX, //1
    EDX, //2
    EBX, //3
    ESP, //4
    EBP, //5
    ESI, //6
    EDI, //7

    //x64
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,

    EIP,
    REGISTER_X86_FLAGS,
    REGISTER_X86_MAX
} reg_t;

enum FLAGS
{
	X86_CARRY_FLAG = 1,
	X86_PARITY_FLAG = 0x4,
	X86_ADJUST_FLAG = 0x10,
	X86_ZERO_FLAG = 0x40,
	X86_SIGN_FLAG = 0x80,
	X86_TRAP_FLAG = 0x100,
	X86_INTERRUPT_ENABLE_FLAG = 0x200,
	X86_DIRECTION_FLAG = 0x400,
	X86_OVERFLOW_FLAG = 0x800
};

static const char *register_x86_names[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi","eip",NULL};

struct variable
{
    int offset;
    int is_param;
    struct ast_node *data_type_node;
};

struct function
{
    int location;
    const char *name;
    struct hash_map *variables;
    int localvariablesize;
};

typedef struct
{
    i32 data_index;
    i32 ip;
    int type;
} reljmp_t;

#define RJ_JNZ (1)
#define RJ_JZ (2)

#define RJ_JNE (1)
#define RJ_JE (2)
#define RJ_JL (4)
#define RJ_JLE (8)
#define RJ_JG (16)
#define RJ_JGE (32)
#define RJ_JMP (64)
#define RJ_REVERSE (1<<30)

struct scope
{
    int numbreaks;
    intptr_t breaks[16]; //TODO: N number of breaks, dynamic array / stack 
    reljmp_t* break_cond;
};

enum RELOC_TYPE
{
    RELOC_CODE,
    RELOC_DATA,
    RELOC_IMPORT
};

struct relocation
{
    enum RELOC_TYPE type;
    size_t size;
    intptr_t from;
    intptr_t to;
};

enum BUILD_TARGET
{
    BT_UNKNOWN,
    BT_LINUX_X86,
    BT_LINUX_X64,
    BT_WIN32,
    BT_WIN64,
    BT_MEMORY,
    BT_OPCODES
};

static const char* build_target_strings[] = { "Unknown", "Linux ELF x86", "Linux ELF x64", "Win32", "Win64", "Memory", "Opcodes", NULL};

struct dynlib_sym
{
    const char* lib_name;
    const char* sym_name;
    intptr_t offset; //TODO: FIXME if and when we ever compile for cross platform or x86/x64 this should probably be changed to match the target binary format.
    hash_t hash;
};

typedef struct dynlib_sym* (*find_import_fn_t)(void *userptr, const char *key);

typedef struct compiler_s
{
    int build_target;
    u32 entry;
    heap_string data;

    struct linked_list *relocations;
    struct linked_list *functions;
    
	heap_string instr;

    struct function *function;

    intptr_t registers[8];

	//TODO: keep track of how many times register is being used to prevent clobbering and unneccessary push/pops
	//TODO: FIXME implement better way to do this with register allocation e.g buckets/graph coloring
	int register_usage[REGISTER_X86_MAX];
	
    struct scope *scope[16]; //TODO: N number of scopes, dynamic array / stack
    int scope_index;

    void* find_import_fn_userptr;
    find_import_fn_t find_import_fn;
	int (*rvalue)(struct compiler_s *ctx, reg_t reg, struct ast_node *n);
	int (*lvalue)(struct compiler_s *ctx, reg_t reg, struct ast_node *n);
	
	void (*print)(struct compiler_s *ctx, const char *fmt, ...);
} compiler_t;
#endif
