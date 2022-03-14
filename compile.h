#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"
#include "data_type.h"
#include "rhd/hash_string.h"
#include "rhd/hash_map.h"
#include "arena.h"

#include <setjmp.h>

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

typedef struct
{
	int nbits;
	int indexed;
	union
	{
		int64_t dq;
		int32_t dd[2];
		int16_t dw[4];
		int8_t db[8];
	};
} vregval_t;

static void setvregval(vregval_t *rv, int i)
{
	rv->indexed = 0;
	rv->nbits = 32;
	rv->dd[0] = i;
}

static int getvregval(vregval_t *rv)
{
	switch(rv->nbits)
	{
		case 32:
		return rv->dd[0];
		case 64:
		return rv->dq;
		case 8:
		return rv->db[0];
		case 16:
		return rv->dw[0];
	}
}

static void setvregvalindex(vregval_t *rv, int index)
{
	rv->indexed = 1;
	rv->nbits = 32;
	rv->dd[0] = index;
}

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

typedef struct variable_s
{
    int offset;
    int is_param;
    struct ast_node *data_type_node;
} variable_t;

#define FUNCTION_NAME_MAX_CHARACTERS (64)

typedef struct function_s
{
	char name[FUNCTION_NAME_MAX_CHARACTERS];
	
    struct hash_map *variables;
    int localvariablesize;
	
	heap_string bytecode;
} function_t;

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

typedef struct
{
	int index;
	size_t length;
	char *buffer;
} indexed_data_t;

#define MAX_INDEXED_DATA 256 //TODO: dynamically increase

typedef struct
{
	//size in bits of each type
	int longsize;
	int intsize;
	int shortsize;
	int charsize;
	int floatsize;
	int doublesize;
	int pointersize;
} fundamental_type_size_t;

#define COMPILER_MAX_FUNCTIONS (64)
#define COMPILER_MAX_SCOPES (16)

typedef struct compiler_s
{
	arena_t *allocator;
	int numbits;
	
    jmp_buf jmp;
	
    int build_target;
    u32 entry;
    heap_string data;

    struct linked_list *relocations;
    
	fundamental_type_size_t fts;
	
	heap_string instr;

    function_t *function;

    intptr_t registers[8];
	indexed_data_t indexed_data[MAX_INDEXED_DATA];
	int numindexeddata;

	//TODO: keep track of how many times register is being used to prevent clobbering and unneccessary push/pops
	//TODO: FIXME implement better way to do this with register allocation e.g buckets/graph coloring
	int register_usage[REGISTER_X86_MAX];
	
    struct scope *scope[COMPILER_MAX_SCOPES]; //TODO: N number of scopes, dynamic array / stack
    int scope_index;

    void* find_import_fn_userptr;
    find_import_fn_t find_import_fn;
	int (*rvalue)(struct compiler_s *ctx, reg_t reg, struct ast_node *n);
	int (*lvalue)(struct compiler_s *ctx, reg_t reg, struct ast_node *n);
	
	void (*print)(struct compiler_s *ctx, const char *fmt, ...);
	
	struct hash_map *functions;
} compiler_t;

static int add_indexed_data(compiler_t *ctx, const void *buffer, size_t len)
{
	if(ctx->numindexeddata + 1 >= MAX_INDEXED_DATA)
		perror("out of memory for indexed data");
	indexed_data_t *id = &ctx->indexed_data[ctx->numindexeddata++];
	id->index = ctx->numindexeddata - 1;
	id->length = len;
	id->buffer = buffer;
	return id->index;
}

static function_t *compiler_alloc_function(compiler_t *ctx, const char *name)
{
	function_t gv;
	snprintf(gv.name, sizeof(gv.name), "%s", name);
	gv.localvariablesize = 0;
	//TODO: free/cleanup variables
	gv.variables = hash_map_create_with_custom_allocator(variable_t, ctx->allocator, arena_alloc);
	
	hash_map_insert(ctx->functions, name, gv);
	
	//TODO: FIXME make insert return a reference to the data inserted instead of having to find it again.
	return hash_map_find(ctx->functions, name);
}

static void compiler_init(compiler_t *c, arena_t *allocator, int numbits)
{
	assert(numbits == 64);
	
	//x64
	c->fts.longsize = 64;
	c->fts.intsize = 32;
	c->fts.shortsize = 16;
	c->fts.charsize = 8;
	c->fts.floatsize = 32;
	c->fts.doublesize = 64;
	c->fts.pointersize = 64;
	
	c->numbits = numbits;
	c->allocator = allocator;
	c->functions = hash_map_create_with_custom_allocator(function_t, c->allocator, arena_alloc);
	
	//mainly just holder for global variables and maybe code without function
	c->function = compiler_alloc_function(c, "_global_variables");
}
#endif
