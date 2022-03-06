//usage: rasm2 -a x86 -b 64 -d "$(./dump a.out)"
#include <stdio.h>
#include <malloc.h>
#include <assert.h>
#include "elf.h"

int read_binary_file(const char* path, unsigned char** pdata, size_t* size)
{
	FILE *fp = fopen(path, "rb");
	if(!fp) return 1;
	fseek(fp, 0, SEEK_END);
	*size = (size_t)ftell(fp);
	rewind(fp);
	unsigned char *data = malloc(*size);
	if(!data)
	{
		fclose(fp);
		return 3;
	}
	if(fread(data, 1, *size, fp) != *size)
	{
		free(data);
		fclose(fp);
		return 2;
	}
	*pdata = data;
	fclose(fp);
	return 0;
}

//temporarily for debugging, read out elf64 values to check stuff

static void print_flags(int flags)
{
	printf("flags: ");
	if(flags & PF_X)
		printf("X");
	if(flags & PF_R)
		printf("R");
	if(flags & PF_W)
		printf("W");
	putchar('\n');
}

static const char* pt_string(int type)
{
	static const char *strings[] = {"null","load","dynamic","interp","note","shlib","phdr","tls",NULL};
	if(type <= 0x7)
		return strings[type];
	return "unknown";
}

static void print_hex(u8 *buf, size_t n)
{
	for (int i = 0; i < n; ++i)
	{
		printf("%02X%s", buf[i] & 0xff, i + 1 == n ? "" : " ");
	}
}

int main(int argc, char **argv)
{
	assert(argc > 1);
	const char *file = argv[1];
	unsigned char *data;
	size_t size;
	if(0 != read_binary_file(file, &data, &size))
	{
		//printf("failed to read file '%s'\n", file);
		return 0;
	}

	//image + 24 = entry
	u64 *entry = data + 24;

	u16 *num_program_headers = data + 56;

	//printf("entry = %02X\n", *entry);
	//printf("num program headers = %d\n", *num_program_headers);
	size_t off = 0x40;
	struct phdr64 *hdr = NULL;
	for(int i = 0; i < *num_program_headers; ++i)
	{
//find code
    hdr = (struct phdr64*)&data[off];
	if(hdr->p_flags & PF_X)
	{
		break;
	}
	//printf("hdr type=%s\n",pt_string(hdr->p_type));
	//printf("alignment=%d\n",hdr->p_align);
	//print_flags(hdr->p_flags);
	off += 0x38;
	}
	assert(hdr->p_flags & PF_X);
	//printf("code offset=%d,%d bytes\n",hdr->p_offset,hdr->p_filesz);
	print_hex(&data[hdr->p_offset], hdr->p_filesz);
	//printf("%d bytes\n", size);
	return 0;
}
