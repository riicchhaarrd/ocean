#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"
#include "data_type.h"
#include "rhd/hash_string.h"

typedef enum
{
    EAX,
    ECX,
    EDX,
    EBX,
    ESP,
    EBP,
    ESI,
    EDI,
	
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

struct scope
{
    int numbreaks;
    intptr_t breaks[16]; //TODO: N number of breaks, dynamic array / stack 
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
    struct scope *scope[16]; //TODO: N number of scopes, dynamic array / stack
    int scope_index;

    void* find_import_fn_userptr;
    find_import_fn_t find_import_fn;
} compiler_t;
#endif
