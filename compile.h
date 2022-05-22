#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"
#include "data_type.h"
#include "rhd/hash_string.h"
#include "rhd/hash_map.h"
#include "arena.h"
#include "instruction.h"
#include "register.h"
#include "virtual_opcodes.h"
#include <setjmp.h>

enum
{	
	VREG_SP,
	VREG_BP,
	VREG_IP,
	VREG_RETURN_VALUE,
	VREG_MAX
};
typedef int vreg_t;
typedef int reg_t;

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
    struct ast_node_s *data_type_node;
} variable_t;

#define FUNCTION_NAME_MAX_CHARACTERS (64)
#define VOPCACHE_MAX (64)

typedef struct
{
	voperand_t key, value;
} vopcache_t;

typedef struct function_s
{
	char name[FUNCTION_NAME_MAX_CHARACTERS];

	struct hash_map *arguments;
    struct hash_map *variables;
    int localvariablesize;
	
	heap_string bytecode;

	vinstr_t *instructions;
	size_t instruction_index;
	size_t index;

	/* vinstr_t *returns[32]; */
	/* size_t numreturns; */

	vopcache_t vopcache[VOPCACHE_MAX];
	size_t vopcacheindex;
	voperand_t eoflabel;
	int argcost;
	int returnsize;
} function_t;

#define FUNCTION_MAX_INSTRUCTIONS (256)

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

typedef struct
{
	/* vinstr_t *breaks[32]; */
	/* size_t maxbreaks; */
	/* size_t numbreaks; */
	voperand_t breaklabel;
} scope_t;

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

typedef enum
{
	COMPILER_FLAGS_NONE,
	COMPILER_FLAGS_ALU_THREE_OPERANDS,
	COMPILER_FLAGS_INDIRECT_ADDRESSING
} compiler_flags_t;

struct compiler_s
{
	arena_t *allocator;
	int numbits;
	int flags;
	
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
	
    scope_t *scope[COMPILER_MAX_SCOPES]; //TODO: N number of scopes, dynamic array / stack
    int scope_index;

    void* find_import_fn_userptr;
    find_import_fn_t find_import_fn;
	int (*rvalue)(struct compiler_s *ctx, vreg_t reg, struct ast_node *n);
	int (*lvalue)(struct compiler_s *ctx, vreg_t reg, struct ast_node *n);
	
	void (*print)(struct compiler_s *ctx, const char *fmt, ...);

	size_t numfunctions;
	struct hash_map *functions;

	size_t vregindex;
	size_t labelindex;
};

typedef struct compiler_s compiler_t;

int add_indexed_data(compiler_t *ctx, const void *buffer, size_t len);
function_t *compiler_alloc_function(compiler_t *ctx, const char *name);
void compiler_init(compiler_t *c, arena_t *allocator, int numbits, compiler_flags_t flags);
#endif
