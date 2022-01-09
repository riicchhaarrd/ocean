#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "compile.h"
#include "rhd/linked_list.h"
#include "util.h"
#include "std.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern int opt_flags;

int build_memory_image(compiler_t *ctx, const char *binary_path)
{
    heap_string instr = ctx->instr;
    heap_string data_buf = ctx->data;

#ifdef _WIN32

    //TODO: FIXME change hardcoded x86 to x64 or other arch later
#define ALIGNMENT (0x1000)

    //TODO: allocate N bytes of size instr and align it to page size
    //TODO: make seperate data buffer and make it non-executable

	SYSTEM_INFO si;
	GetSystemInfo(&si);

    u32 il = heap_string_size(&instr);
    size_t dl = heap_string_size(&data_buf);
    size_t page_size = si.dwPageSize;
    size_t sztotal = il + dl;
    char* buffer = VirtualAlloc(NULL, sztotal, MEM_COMMIT, PAGE_READWRITE);
    memcpy(buffer, instr, il);
    intptr_t code_offs = (intptr_t)buffer;
    intptr_t data_offs = 0;
    if (dl > 0)
    {
        memcpy(&buffer[il], data_buf, dl);
        data_offs = code_offs + il;
    }


    linked_list_reversed_foreach(ctx->relocations, struct relocation*, it,
    {
            if (it->type == RELOC_DATA)
            {
                *(u32*)&buffer[it->from] = it->to + data_offs;
                if (opt_flags & OPT_VERBOSE)
                printf("[DATA] relocating %d bytes from %02X to %02X\n", it->size, it->from, it->to + data_offs);
            }
            else if (it->type == RELOC_CODE)
            {
                *(u32*)&buffer[it->from] = it->to + code_offs;
                if (opt_flags & OPT_VERBOSE)
                printf("[CODE] relocating %d bytes from %02X to %02X\n", it->size, it->from, it->to + code_offs);
            }
            else if (it->type == RELOC_IMPORT)
            {
                struct dynlib_sym* sym = (struct dynlib_sym*)it->to;
                intptr_t realcodepos = code_offs + it->from;
                *(u32*)&buffer[it->from] = realcodepos + 6;
                *(u32*)&buffer[it->from + 6] = sym->offset;
                if (opt_flags & OPT_VERBOSE)
                printf("[IMPORT] relocating %d bytes\n", it->size);
            }
            else
            {
                printf("unknown relocation type %d\n", it->type);
            }
    });
    DWORD dummy;
    VirtualProtect(buffer, sztotal, PAGE_EXECUTE_READ, &dummy);
    int (__cdecl *fn)(void) = (int(__cdecl *)(void))buffer;
    int result = fn();
    if (opt_flags & OPT_VERBOSE)
    printf("result: %d\n", result);
    VirtualFree(buffer, 0, MEM_RELEASE);
	#else
		printf("memory build target is unsupported on this platform.\n");
		exit(1);
	#endif
	return 0;
}
