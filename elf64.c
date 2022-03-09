#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "compile.h"
#include "rhd/std.h"
#include "rhd/linked_list.h"
#include "util.h"
#include "elf.h"

int build_elf64_image(compiler_t *ctx, const char *binary_path)
{
	assert(sizeof(struct phdr64) == 0x38);
	
    heap_string instr = ctx->instr;
	#if 0
	instr = NULL;
	
	db(&instr, 0xcc);
	db(&instr, 0xcc);
	db(&instr, 0xcc);
	
	db(&instr, 0x48);
	db(&instr, 0xc7);
	db(&instr, 0xc0);
	db(&instr, 0x3c);
	db(&instr, 0x00);
	db(&instr, 0x00);
	db(&instr, 0x00);
	//syscall
	db(&instr, 0x0f);
	db(&instr, 0x05);
	#endif
	
    heap_string data_buf = ctx->data;
	heap_string image = NULL;
    db(&image, 0x7f);
    db(&image, 'E');
    db(&image, 'L');
    db(&image, 'F');
    db(&image, 2); //64 bits
    db(&image, 1);
    db(&image, 1);
    db(&image, 0); //System V

    for(size_t i = 0; i < 8; ++i)
	    db(&image, 0);
    dw(&image, 2); //e_type
    dw(&image, 0x3e); //e_machine //AMD x86-64 //https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
    dd(&image, 1); //e_version

    size_t entry_offset = heap_string_size(&image);
    dq(&image, 0); //we'll fill this in later //e_entry
    dq(&image, 0x40); //e_phoff

    dq(&image, 0); //e_shoff

    dd(&image, 0); //e_flags
    dw(&image, 0x34); //e_ehsize
    dw(&image, sizeof(struct phdr64)); //e_phentsize

    u32 num_program_headers = 2;
    if(ctx->data)
        ++num_program_headers;
    
    dw(&image, num_program_headers); //e_phnum //amount of program headers
    dw(&image, 0); //e_shentsize
    dw(&image, 0); //e_shnum
    dw(&image, 0); //e_shstrndx
    
    #define ORG (0x40000)
	#define ALIGNMENT (0x1000)

    int phdr_offset = heap_string_size(&image);
    
    int null_hdr_offset = heap_string_size(&image);
    pad(&image, sizeof(struct phdr64));
    int text_hdr_offset = heap_string_size(&image);
    pad(&image, sizeof(struct phdr64));

    int data_hdr_offset = heap_string_size(&image);
    if(ctx->data)
    	pad(&image, sizeof(struct phdr64));
    
    int phdr_end = heap_string_size(&image);
    
    //get pointers now because image pointer is different than before after the reallocation
    struct phdr64 *null_hdr = (struct phdr64*)&image[null_hdr_offset];

    null_hdr->p_type = PT_NULL;
    null_hdr->p_flags = PF_R;
    null_hdr->p_offset = 0;
    null_hdr->p_vaddr = ORG;
    null_hdr->p_paddr = ORG;
    null_hdr->p_filesz = phdr_end;
    null_hdr->p_memsz = phdr_end;
    null_hdr->p_align = ALIGNMENT;
    
	u32 il = heap_string_size(&instr);

    size_t entry = heap_string_size(&image);
    *(uint32_t*)(image + entry_offset) = ORG + ALIGNMENT;

    pad_align(&image, ALIGNMENT);
    
	u32 code_offset = heap_string_size(&image);

    struct phdr64 *text_hdr = (struct phdr64*)&image[text_hdr_offset];
    text_hdr->p_type = PT_LOAD;
    text_hdr->p_flags = PF_R | PF_X;
    text_hdr->p_offset = code_offset;
    text_hdr->p_vaddr = ORG + ALIGNMENT;
    text_hdr->p_paddr = ORG + ALIGNMENT;
    text_hdr->p_filesz = il;
    text_hdr->p_memsz = il;
    text_hdr->p_align = ALIGNMENT;
    
    //put .data section here
    if(ctx->data)
    {
        int vaddr = ORG + ALIGNMENT + il;
        vaddr += align_to(vaddr, ALIGNMENT);
		
        //relocate everything to vaddr
		//TODO: FIXME 64-bit addresses
		linked_list_reversed_foreach(ctx->relocations, struct relocation*, it,
        {
            if(it->type == RELOC_DATA)
			{
				*(u32*)&instr[it->from] = it->to + vaddr;
                printf("[DATA] relocating %d bytes from %02X to %02X\n", it->size, it->from, it->to + vaddr);
			}
			else if(it->type == RELOC_CODE)
			{
				*(u32*)&instr[it->from] = it->to + (ORG + ALIGNMENT);
                printf("[CODE] relocating %d bytes from %02X to %02X\n", it->size, it->from, it->to + ORG);
			} else
			{
                printf("unknown relocation type %d\n", it->type);
			}
        });
        
        for(int i = 0; i < il; ++i)
        {
            db(&image, instr[i]);
        }
        
    	pad_align(&image, ALIGNMENT);
        
		u32 data_offset = heap_string_size(&image);
    	u32 dl = heap_string_size(&data_buf);
        
        struct phdr64 *data_hdr = (struct phdr64*)&image[data_hdr_offset];
        data_hdr->p_type = PT_LOAD;
        data_hdr->p_flags = PF_R | PF_W;
        data_hdr->p_offset = data_offset;
        data_hdr->p_vaddr = vaddr;
        data_hdr->p_paddr = vaddr;
        data_hdr->p_filesz = dl;
        data_hdr->p_memsz = dl;
        data_hdr->p_align = ALIGNMENT;
        
        for(int i = 0; i < dl; ++i)
        {
            db(&image, data_buf[i]);
        }
    } else
    {
		#if 0
        linked_list_reversed_foreach(ctx->relocations, struct relocation*, it,
        {
            if(it->type == RELOC_CODE)
			{
				*(u32*)&instr[it->from] = it->to + (ORG + ALIGNMENT);
                printf("[CODE] relocating %d bytes from %02X to %02X\n", it->size, it->from, it->to + ORG);
			} else
			{
                printf("unknown relocation type %d\n", it->type);
			}
        });
        for(int i = 0; i < il; ++i)
        {
            db(&image, instr[i]);
        }
		#endif
		printf("unhandled for now..\n");
		getchar();
    }
    
    size_t filesize = heap_string_size(&image);
    FILE* fp;
    std_fopen_s(&fp, binary_path, "wb");
    if(!fp)
    {
        char errorMessage[1024];
        std_strerror_s(errorMessage, sizeof(errorMessage), errno);
        printf("failed to open '%s', error = %s\n", binary_path, errorMessage);
        return 1;
    }
    fwrite(image, filesize, 1, fp);
    fclose(fp);

    heap_string_free(&image);
	return 0;
}
