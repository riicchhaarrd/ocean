#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"
#include "data_type.h"
#include "rhd/hash_string.h"
#include "rhd/hash_map.h"
#include "arena.h"
#include "codegen.h"

#include <setjmp.h>

//TODO: implement later
enum vreg_s
{
	VREG8_ANY,
	VREG8_0,
	VREG8_1,
	VREG8_2,
	VREG8_3,
	
	VREG16_ANY,
	VREG16_0,
	VREG16_1,
	VREG16_2,
	VREG16_3,
	
	VREG32_ANY,
	VREG32_0,
	VREG32_1,
	VREG32_2,
	VREG32_3,
	
	VREG64_ANY,
	VREG64_0,
	VREG64_1,
	VREG64_2,
	VREG64_3,
	
	//use VREG_ANY unless you specifically need to move things to another register e.g mov VREG_0, VREG_1
	//or when you need atleast N amount of bits use any of the 8/16/32/64 VREG values
	
	VREG_ANY,
	VREG_0,
	VREG_1,
	VREG_2,
	VREG_3,
	
	VREG_SP,
	VREG_BP,
	VREG_IP,
	VREG_MAX
};
typedef enum vreg_s vreg_t;
typedef int reg_t;

static int vreg_count_bits(vreg_t reg)
{
	if(reg <= VREG8_3)
		return 8;
	if(reg <= VREG16_3)
		return 16;
	if(reg <= VREG32_3)
		return 32;
	if(reg <= VREG64_3)
		return 64;
	//any other vreg amount of bits will be determined by the compiler target
	return -1;
}

static const char *vreg_names[] = {
	"vreg8_any",
	"vreg8_0",
	"vreg8_1",
	"vreg8_2",
	"vreg8_3",
	"vreg16_any",
	"vreg16_0",
	"vreg16_1",
	"vreg16_2",
	"vreg16_3",
	"vreg32_any",
	"vreg32_0",
	"vreg32_1",
	"vreg32_2",
	"vreg32_3",
	"vreg64_any",
	"vreg64_0",
	"vreg64_1",
	"vreg64_2",
	"vreg64_3",
	"vreg_any",
	"vreg_0",
	"vreg_1",
	"vreg_2",
	"vreg_3",
	"vreg_sp",
	"vreg_bp",
	"vreg_ip",
	NULL
};

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

struct reljmp_s
{
    i32 data_index;
    i32 ip;
    int type;
};
typedef struct reljmp_s reljmp_t;

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

struct compiler_s
{
	codegen_t cg;
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

	indexed_data_t indexed_data[MAX_INDEXED_DATA];
	int numindexeddata;
	
    struct scope *scope[COMPILER_MAX_SCOPES]; //TODO: N number of scopes, dynamic array / stack
    int scope_index;

    void* find_import_fn_userptr;
    find_import_fn_t find_import_fn;
	int (*rvalue)(struct compiler_s *ctx, vreg_t reg, struct ast_node *n);
	int (*lvalue)(struct compiler_s *ctx, vreg_t reg, struct ast_node *n);
	
	void (*print)(struct compiler_s *ctx, const char *fmt, ...);
	
	struct hash_map *functions;
};

typedef struct compiler_s compiler_t;

int add_indexed_data(compiler_t *ctx, const void *buffer, size_t len);
function_t *compiler_alloc_function(compiler_t *ctx, const char *name);
void compiler_init(compiler_t *c, arena_t *allocator, int numbits);
#endif
