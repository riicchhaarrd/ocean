#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "rhd/heap_string.h"
#include "types.h"

static void dd(heap_string *s, uint32_t i)
{
    union
    {
	uint32_t i;
	uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
	heap_string_push(s, u.b[i]);
}

static void dw(heap_string *s, uint16_t i)
{
    union
    {
	uint16_t s;
	uint8_t b[2];
    } u = { .s = i };

    heap_string_push(s, u.b[0]);
    heap_string_push(s, u.b[1]);
}

static void db(heap_string *s, u8 op)
{
    heap_string_push(s, op);
}

static void pad(heap_string *s, u32 n)
{
    for(int i = 0; i < n; ++i)
        heap_string_push(s, i & 0xff);
}

static void pad_align(heap_string *s, int align)
{
    int pos = heap_string_size(s);
    if(pos % align == 0)
        return; //no alignment needed
    int m = align - (pos % align);
    for(int i = 0; i < m; ++i)
        heap_string_push(s, 0);
}

static void buf(heap_string *s, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(s, buf[i] & 0xff);
    }
}

struct __attribute__((__packed__)) phdr32
{
    i32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    i32 p_flags;
    u32 p_align;
};

enum
{
    PF_X = 0x1,
    PF_W = 0x2,
    PF_R = 0x4
};

enum
{
    PT_NULL = 0x0,
    PT_LOAD = 0x1
};

int build_elf_image(heap_string instr, heap_string data_buf, const char *binary_path)
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
    dd(&image, 0x34); //e_phoff

    dd(&image, 0); //e_shoff

    dd(&image, 0); //e_flags
    dw(&image, 0x34); //e_ehsize
    dw(&image, sizeof(struct phdr32)); //e_phentsize

    u32 num_program_headers = 2;
    
    dw(&image, num_program_headers); //e_phnum //amount of program headers
    dw(&image, 0); //e_shentsize
    dw(&image, 0); //e_shnum
    dw(&image, 0); //e_shstrndx
    
    #define ORG (0x08048000)
	#define ALIGNMENT (0x1000)

    int phdr_offset = heap_string_size(&image);
    
    int null_hdr_offset = heap_string_size(&image);
    pad(&image, sizeof(struct phdr32));
    int text_hdr_offset = heap_string_size(&image);
    pad(&image, sizeof(struct phdr32));
    int data_hdr_offset = heap_string_size(&image);
    pad(&image, sizeof(struct phdr32));

    int phdr_end = heap_string_size(&image);
    
    //get pointers now because image pointer is different than before after the reallocation
    struct phdr32 *null_hdr = (struct phdr32*)&image[null_hdr_offset];

    null_hdr->p_type = PT_LOAD;
    null_hdr->p_offset = 0;
    null_hdr->p_vaddr = ORG;
    null_hdr->p_paddr = ORG;
    null_hdr->p_filesz = phdr_end;
    null_hdr->p_memsz = phdr_end;
    null_hdr->p_flags = PF_R;
    null_hdr->p_align = ALIGNMENT;
    
	u32 il = heap_string_size(&instr);
    u32 dl = heap_string_size(&data_buf);

    size_t entry = heap_string_size(&image);
    *(uint32_t*)(image + entry_offset) = ORG + ALIGNMENT;

    pad_align(&image, ALIGNMENT);
    
	u32 code_offset = heap_string_size(&image);

    struct phdr32 *text_hdr = (struct phdr32*)&image[text_hdr_offset];
    text_hdr->p_type = PT_LOAD;
    text_hdr->p_offset = code_offset;
    text_hdr->p_vaddr = ORG + ALIGNMENT;
    text_hdr->p_paddr = ORG + ALIGNMENT;
    text_hdr->p_filesz = il;
    text_hdr->p_memsz = il;
    text_hdr->p_flags = PF_R | PF_X;
    text_hdr->p_align = ALIGNMENT;
    
    //put .text/code section here
	for(int i = 0; i < il; ++i)
	{
		db(&image, instr[i]);
	}

    /*
    //put .data section here
	for(int i = 0; i < dl; ++i)
	{
		db(&image, data_buf[i]);
	}
    */
    
    size_t filesize = heap_string_size(&image);
    FILE * fp = fopen(binary_path, "wb");
    if(!fp)
    {
        printf("failed to open '%s', error = %s\n", binary_path, strerror(errno));
        return 1;
    }
    fwrite(image, filesize, 1, fp);
    fclose(fp);

    heap_string_free(&image);
	return 0;
}
