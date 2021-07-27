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

struct variable
{
    int offset;
    int is_param;
    int data_type;
    int data_size;
};

struct function
{
    int location;
    const char *name;
    struct hash_map *variables;
    int localsize;
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
};
#endif
