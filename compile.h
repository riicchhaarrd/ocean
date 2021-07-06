#ifndef COMPILE_H
#define COMPILE_H

#include "types.h"
#include "rhd/heap_string.h"

enum REGISTERS
{
    EAX,
    EBX,
    ECX,
    EDX,
    ESI,
    ESP,
    EBP,
    EIP
};

struct variable
{
    int offset;
};

struct function
{
    int location;
    const char *name;
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
    struct hash_map *variables;
    heap_string data;

    struct linked_list *relocations;
    struct linked_list *functions;
    
	heap_string instr;
    int localsize;
};
#endif
