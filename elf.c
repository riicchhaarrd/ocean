#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "rhd/heap_string.h"

typedef uint8_t u8;

void dd(heap_string *s, uint32_t i)
{
    union
    {
	uint32_t i;
	uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
	heap_string_push(s, u.b[i]);
}

void dw(heap_string *s, uint16_t i)
{
    union
    {
	uint16_t s;
	uint8_t b[2];
    } u = { .s = i };

    heap_string_push(s, u.b[0]);
    heap_string_push(s, u.b[1]);
}

void db(heap_string *s, u8 op)
{
    heap_string_push(s, op);
}

void buf(heap_string *s, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
	heap_string_push(s, buf[i] & 0xff);
    }
}

int build_elf_image(heap_string instr, const char *binary_path)
{
	heap_string image = NULL;
    db(&image, 0x7f);
    db(&image, 'E');
    db(&image, 'L');
    db(&image, 'F');
    db(&image, 1);
    db(&image, 1);
    db(&image, 1);
    db(&image, 0);

    for(size_t i = 0; i < 8; ++i)
	    db(&image, 0);
    dw(&image, 2); //e_type
    dw(&image, 3); //e_machine
    dd(&image, 1); //e_version

    size_t entry_offset = heap_string_size(&image);
    dd(&image, 0); //we'll fill this in later //e_entry
    
    size_t ph_offset = heap_string_size(&image);
    dd(&image, 0); //we'll fill this in later //e_phoff

    dd(&image, 0); //e_shoff

    dd(&image, 0); //e_flags
    
    size_t ehsize_offset = heap_string_size(&image);
    dw(&image, 0); //we'll fill this in later //e_ehsize

    size_t phentsize_offset = heap_string_size(&image);
    dw(&image, 0); //we'll fill this in later //e_phentsize

    dw(&image, 1); //e_phnum
    dw(&image, 0); //e_shentsize
    dw(&image, 0); //e_shnum
    dw(&image, 0); //e_shstrndx

    //fill in some known values now
    *(uint16_t*)(image + ehsize_offset) = heap_string_size(&image);

    int phdr_offset = heap_string_size(&image);
    *(uint16_t*)(image + ph_offset) = phdr_offset;
    
    uint32_t org = 0x08048000;

    dd(&image, 1); //p_type
    dd(&image, 0); //p_offset
    dd(&image, org); //p_vaddr
    dd(&image, org); //p_paddr

    size_t filesz_offset = heap_string_size(&image);
    dd(&image, 0); //p_filesz

    size_t memsz_offset = heap_string_size(&image);
    dd(&image, 0); //p_memsz

    dd(&image, 5); //p_flags
    dd(&image, 0x1000); //p_align

    //fill in some more known values
    
    int phentsize = heap_string_size(&image) - phdr_offset;
    *(uint16_t*)(image + phentsize_offset) = phentsize;

    size_t entry = heap_string_size(&image);
    *(uint32_t*)(image + entry_offset) = entry + org;

    //append opcodes here for the program

	int il = heap_string_size(&instr);
	//printf("instruction length = %d\n", il);
	for(int i = 0; i < il; ++i)
	{
		db(&image, instr[i]);
	}

	//db(&image, 0xcc); //int3
	
    db(&image, 0xb3); //mov bl,42
    db(&image, 0x2a);
    db(&image, 0x31); //xor eax,eax
    db(&image, 0xc0);
    db(&image, 0x40); //inc eax
    db(&image, 0xcd); //int 0x80
    db(&image, 0x80);

    //this code won't ever be reached, let's just put our data here for now

    size_t filesize = heap_string_size(&image);
    *(uint32_t*)(image + filesz_offset) = filesize;
    *(uint32_t*)(image + memsz_offset) = filesize;

    FILE * fp = fopen(binary_path, "wb");
    if(!fp) return 1;
    fwrite(image, filesize, 1, fp);
    fclose(fp);

    heap_string_free(&image);
	return 0;
}
