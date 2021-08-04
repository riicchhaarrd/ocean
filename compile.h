#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"
#include "data_type.h"

enum REGISTER
{
    EAX,
    ECX,
    EDX,
    EBX,
    ESP,
    EBP,
    ESI,
    EDI
};

enum
{
    IMM32 = 4,
    IMM16 = 2,
    IMM8 = 1
};

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
    RELOC_DATA
};

struct relocation
{
    enum RELOC_TYPE type;
    int size;
    int from;
    int to;
};

struct compile_context
{
    u32 entry;
    heap_string data;

    struct linked_list *relocations;
    struct linked_list *functions;
    
	heap_string instr;

    struct function *function;

    intptr_t registers[8];
    struct scope *scope[16]; //TODO: N number of scopes, dynamic array / stack
    int scope_index;
};
#endif
