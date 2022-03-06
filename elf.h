#ifndef ELF_H
#define ELF_H

#include "types.h"

PACK(struct phdr64
{
    u32 p_type;
    u32 p_flags;
	u64 p_offset;
	u64 p_vaddr;
	u64 p_paddr;
	u64 p_filesz;
	u64 p_memsz;
	u64 p_align;
});

PACK(struct phdr32
{
    i32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    i32 p_flags;
    u32 p_align;
});

enum
{
    PF_X = 0x1,
    PF_W = 0x2,
    PF_R = 0x4
};

enum
{
    PT_NULL = 0x0,
    PT_LOAD = 0x1,
	PT_DYNAMIC = 0x2,
	PT_INTERP = 0x3,
	PT_NOTE = 0x4,
	PT_SHLIB = 0x5,
	PT_PHDR = 0x6,
	PT_TLS = 0x7
};

#endif
