#ifndef COMPILE_H
#define COMPILE_H

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

struct relocation
{
    int type; //FIXME: unused for now
    int size;
    int from;
    int to;
};

struct compile_context
{
    struct hash_map *variables;
    heap_string data;

    struct linked_list *relocations;
    
	heap_string instr;
    int localsize;
};
#endif
