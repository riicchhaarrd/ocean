#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "compile.h"
#include "rhd/std.h"
#include "rhd/linked_list.h"
#include "util.h"
#include <time.h>

//https://docs.microsoft.com/en-us/windows/win32/debug/pe-format

#define IMAGE_FILE_MACHINE_I386 (0x14c)
#define IMAGE_FILE_MACHINE_AMD64 (0x8664)

#define IMAGE_FILE_RELOCS_STRIPPED (0x0001)
#define IMAGE_FILE_EXECUTABLE_IMAGE (0x002)
#define IMAGE_FILE_32BIT_MACHINE (0x0100)

#define IMAGE_SUBSYSTEM_WINDOWS_CUI (0x3)

#define IMAGE_DLLCHARACTERISTICS_NX_COMPAT (0x0100)
#define IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE (0x0040)
#define IMAGE_DLLCHARACTERISTICS_NO_SEH (0x0400)
#define IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE (0x8000)

PACK(struct pe_hdr
{
    u8 sig[4]; //pe\0\0
    u16 machine; //IMAGE_FILE_MACHINE_I386
    u16 numsections; //1
    u32 timestamp; //time(0)
    u8 pad[8]; //deprecated
    u16 size_of_optional_header; //0xe0
    u16 characteristics; //IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_EXECUTABLE_IMAGE
});

PACK(struct data_dir
{
    u32 rva;
    u32 sz;
});

enum data_dir_type
{
    DDT_EXPORT,
    DDT_IMPORT,
    DDT_RESOURCE,
    DDT_EXCEPTION,
    DDT_SECURITY,
    DDT_RELOC,
    DDT_DEBUG,
    DDT_ARCH,
    DDT_GLOB_PTR,
    DDT_TLS,
    DDT_CFG,
    DDT_BOUND_IMPORT,
    DDT_IAT,
    DDT_DELAY_IMPORT
};

PACK(struct opt_hdr
{
    u16 magic; //0x10b
    u8 major_linker_version; //0xe
    u8 minor_linker_version; //0x1d
    u32 size_of_code; //0x200
    u32 size_of_initialized_data; //0x400
    u32 size_of_uninitialized_Data; //0x0
    u32 address_of_entry_point; //0x1000
    u32 base_of_code; //0x1000
    u32 base_of_data; //0x2000
    u32 image_base; //0x400000
    u32 section_alignment; //0x1000
    u32 file_alignment; //0x200
    u16 major_operating_system_version; //0x4
    u16 minor_operating_system_version; //0x0
    u16 major_image_version; //0x0
    u16 minor_image_version; //0x0
    u16 major_subsystem_version; //0x4
    u16 minor_subsystem_version; //0x0
    u32 win32_version_value; //0x0
    u32 size_of_image; //0x4000
    u32 size_of_headers; //0x400
    u32 checksum; //0x0
    u16 subsystem; //IMAGE_SUBSYSTEM_WINDOWS_CUI
    u16 dll_characteristics; //IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NO_SEH | IMAGE_DLLCHARACTERISTICS_NX_COMPAT | IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE
    u32 size_of_stack_reserve; //0x100000
    u32 size_of_stack_commit; //0x1000
    u32 size_of_heap_reserve; //0x100000
    u32 size_of_heap_commit; //0x1000
    u32 loader_flags; //0x0
    u32 number_of_rva_and_sizes; //0x10
    struct data_dir dir[16];
});

#define IMAGE_SCN_MEM_EXECUTE ( 0x20000000 )
#define IMAGE_SCN_MEM_READ ( 0x40000000 )
#define IMAGE_SCN_CNT_CODE ( 0x00000020 )

PACK(struct section_hdr
{
    char name[8];
    u32 virtual_size;
    u32 virtual_address;
    u32 size_of_raw_data;
    u32 pointer_to_raw_data;
    u32 pointer_to_relocations;
    u32 pointer_to_linenumbers;
    u16 number_of_relocations;
    u16 number_of_linenumbers;
    u32 characteristics;
});

int build_exe_image(struct compile_context *ctx, const char *binary_path)
{
    heap_string instr = ctx->instr;
    heap_string data_buf = ctx->data;
    
    #define ORG (0x400000)
	#define ALIGNMENT (0x1000)
    
	struct pe_hdr pe = { 0 };

	pe.sig[0] = 'P';
	pe.sig[1] = 'E';
	pe.sig[2] = '\0';
	pe.sig[3] = '\0';

	pe.machine = IMAGE_FILE_MACHINE_I386;
	pe.numsections = 1;
	pe.timestamp = (int)time( 0 );
	pe.size_of_optional_header = 0xe0;
	pe.characteristics = IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_EXECUTABLE_IMAGE;

	struct opt_hdr opt = { 0 };
	opt.magic = 0x10b;
	opt.major_linker_version = 0xe;
	opt.minor_linker_version = 0x1d;
	opt.size_of_code = 0x200;
	opt.size_of_initialized_data = 0x400;
	opt.size_of_initialized_data = 0x0;
	opt.address_of_entry_point = 0x1000;
	opt.base_of_code = 0x1000;
	opt.base_of_data = 0x2000;
	opt.image_base = ORG;
	opt.section_alignment = ALIGNMENT;
	opt.file_alignment = 0x200;
	opt.major_operating_system_version = 0x4; // or 0x6
	opt.minor_operating_system_version = 0x0;
	opt.major_image_version = 0x4;
	opt.minor_image_version = 0x0;
	opt.major_subsystem_version = 0x4;
	opt.minor_subsystem_version = 0x0;
	opt.win32_version_value = 0x0;
	opt.size_of_image = 0x4000;
	opt.size_of_headers = 0x400;
	opt.checksum = 0x0;
	opt.subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
	opt.dll_characteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NO_SEH |
					IMAGE_DLLCHARACTERISTICS_NX_COMPAT | IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE;
	opt.size_of_stack_reserve = 0x100000;
	opt.size_of_stack_commit = 0x1000;
	opt.size_of_heap_reserve = 0x100000;
	opt.size_of_heap_reserve = 0x1000;
	opt.loader_flags = 0x0;
	opt.number_of_rva_and_sizes = 0x10;

	//opt.dir[DDT_IMPORT].rva = 0x20e0;
	//opt.dir[DDT_IMPORT].sz = 40;

    
	heap_string image = NULL;

    db(&image, 'M');
    db(&image, 'Z');
    pad(&image, 0x3c - 2);
    
    dd(&image, 64); //pe offset

    buf(&image, (const char*)&pe, sizeof(pe));
    buf(&image, (const char*)&opt, sizeof(opt));

	struct section_hdr section = { 0 };
	snprintf( section.name, sizeof( section.name ), ".text" );
    section.virtual_size = 4; //instr size
    section.virtual_address = 0x1000;
    section.size_of_raw_data = 4;
    section.pointer_to_raw_data = 512;
    section.characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
    buf(&image, (const char*)&section, sizeof(section));

    pad_align(&image, 0x200);
    db(&image, 0x6a);
    db(&image, 0x7f);
    db(&image, 0x58);
    db(&image, 0xc3);
    printf("pos = %d,%02X\n",heap_string_size(&image),heap_string_size(&image));

	int filesize = heap_string_size(&image);
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
