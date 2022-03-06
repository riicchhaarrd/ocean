#include <stdio.h>
#include <stdlib.h>
#include "std.h"
#include "token.h"
#include "ast.h"
#include "types.h"
#include "parse.h"

#define HEAP_STRING_IMPL
#include "rhd/heap_string.h"

#define LINKED_LIST_IMPL
#include "rhd/linked_list.h"

#define HASH_MAP_IMPL
#include "rhd/hash_map.h"

#include "compile.h"
#include "codegen.h"
#include "rhd/hash_string.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma comment(lib, "kernel32.lib")
#endif

// imported functions from other files

int compile_ast(struct ast_node *head, compiler_t *ctx, codegen_t*);
void codegen_x64(codegen_t *cg);

int opt_flags = 0;

int read_symbols_for_dynamic_library(const char *lib_name, struct linked_list **symbols)
{
	struct linked_list* sym_list = *symbols;
#ifdef _WIN32
	HMODULE lib = LoadLibraryA(lib_name); //if we just want to read the symbols without loading it into memory, then use the line below.
	//HMODULE lib = LoadLibraryExA(lib_name, NULL, DONT_RESOLVE_DLL_REFERENCES);
	if (!lib)
		return 1;
	//assert(((PIMAGE_DOS_HEADER)lib)->e_magic == IMAGE_DOS_SIGNATURE);
	PIMAGE_NT_HEADERS header = (PIMAGE_NT_HEADERS)((BYTE*)lib + ((PIMAGE_DOS_HEADER)lib)->e_lfanew);
	//assert(header->Signature == IMAGE_NT_SIGNATURE);
	//assert(header->OptionalHeader.NumberOfRvaAndSizes > 0);
	PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)((BYTE*)lib + header->
		OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	//assert(exports->AddressOfNames != 0);
	BYTE** names = (BYTE**)((int)lib + exports->AddressOfNames);
	for (int i = 0; i < exports->NumberOfNames; i++)
	{
		const char* export_name = (BYTE*)lib + (int)names[i];
		intptr_t loc = (intptr_t)GetProcAddress(lib, export_name);
		assert(loc);
		struct dynlib_sym sym = {
			.lib_name = lib_name,
			.sym_name = export_name,
			.offset = loc,
			.hash = hash_string(export_name)
		};
		linked_list_prepend(sym_list, sym);
		//printf("\tadded '%s' to import list\n", export_name);
	}
	*symbols = sym_list;
#endif
	return 0;
}

//TODO: FIXME implement linux / libdl.so and proper windows exe IAT table
struct dynlib_sym* find_lib_symbol(void *userptr, const char* key)
{
	struct linked_list* symbols = (struct linked_list*)userptr;
	hash_t hash = hash_string(key);
	linked_list_reversed_foreach(symbols, struct dynlib_sym*, it,
	{
		if (it->hash == hash && !strcmp(it->sym_name, key))
			return it;
	});
	return NULL;
}

int main( int argc, char** argv )
{
    assert(argc > 0);
	assert(argc < 32);
    const char *files[32];
    int numfiles = 0;
	//use build target memory as default
	int build_target = BT_LINUX_X64;
	struct linked_list* symbols = linked_list_create(struct dynlib_sym);
	size_t nsymbols = 0;
	
	#ifdef _WIN32
	//link some commonly used libraries by default
	//TODO: FIXME relocate this
	read_symbols_for_dynamic_library("msvcrt.dll", &symbols);
	read_symbols_for_dynamic_library("kernel32.dll", &symbols);
	read_symbols_for_dynamic_library("user32.dll", &symbols);
	read_symbols_for_dynamic_library("opengl32.dll", &symbols);
	#endif
	
	for ( int i = 1; i < argc; ++i )
	{
		if ( argv[i][0] == '-' )
		{
			switch ( argv[i][1] )
			{
            case 'a':
                opt_flags |= OPT_AST;
                break;
            case 'i':
                //only print the asm instructions, don't write to file
                opt_flags |= OPT_INSTR;
                break;
			case 'd':
                //mainly used atm for inserting int3 breakpoints
				opt_flags |= OPT_DEBUG;
				break;
			case 'v':
				opt_flags |= OPT_VERBOSE;
				break;
			case 'b':
			{
				const char* build_target_str = (const char*)&argv[i][2];
				if(opt_flags & OPT_VERBOSE)
				printf( "using build target: %s\n", build_target_str);
				if (!strcmp(build_target_str, "windows"))
					build_target = BT_WIN32;
				else if (!strcmp(build_target_str, "linux"))
					build_target = BT_LINUX_X64;
				else if (!strcmp(build_target_str, "memory"))
					build_target = BT_MEMORY;
				else if(!strcmp(build_target_str, "opcodes"))
					build_target = BT_OPCODES;
				if (opt_flags & OPT_VERBOSE)
				printf("build_target = %d\n", build_target);
			}
			break;
			case 'l':
			{
				const char* lib_name = (const char*)&argv[i][2];
				read_symbols_for_dynamic_library(lib_name, &symbols);
				size_t nsymbols_old = nsymbols;
				linked_list_reversed_foreach(symbols, struct dynlib_sym*, it,
				{
					//printf("\tsym: %s\n", it->sym_name);
					++nsymbols;
				});
				if (opt_flags & OPT_VERBOSE)
				printf("linking against '%s', found %d symbols.\n", lib_name, nsymbols - nsymbols_old);
			}
			break;
			}
		}
		else
		{
			if (opt_flags & OPT_VERBOSE)
			printf("adding %s\n", argv[i]);
			files[numfiles++] = argv[i];
		}
	}
    //TODO: multiple source files
	const char* src = files[numfiles > 1 ? numfiles - 2 : numfiles - 1];
	const char* dst = NULL;
	if(build_target != BT_MEMORY)
		dst = files[numfiles - 1];
	if (src == dst)
		dst = "a.out";
	if (opt_flags & OPT_VERBOSE)
    printf("src: %s, dst: %s\n", src, dst);
    
    /* pre.c */
	heap_string preprocess_file( const char* filename, const char** includepaths, int verbose, struct hash_map *defines, struct hash_map **defines_out);
	const char* includepaths[] = { "examples/include/", NULL };
	heap_string data = preprocess_file( src, includepaths, 0, NULL, NULL );

	if ( !data )
    {
	    printf( "failed to read file '%s'\n", src );
	    return 1;
    }

	struct token *tokens = NULL;
    int num_tokens = 0;
    
	// printf("data = %s\n", data);
	parse( data , &tokens, &num_tokens, LEX_FL_NONE);
	heap_string_free( &data );
    
    //printf("num_tokens = %d\n", num_tokens);
    char str[256]={0};
    for(int i = 0; i < num_tokens; ++i)
    {
        struct token *tk = &tokens[i];
        token_to_string(tk, str, sizeof(str));
		//printf("token %s\n", str);
    }

    struct linked_list *ast_list = NULL;
    struct ast_node *root = NULL;
	compiler_t ctx = { 0 };
    ctx.build_target = build_target;
	ctx.find_import_fn = find_lib_symbol;
	ctx.find_import_fn_userptr = symbols;
	int ast = generate_ast(tokens, num_tokens, &ast_list, &root, opt_flags & OPT_AST);
    if(!ast && (opt_flags & OPT_AST) != OPT_AST)
    {
		// generate native code
		heap_string data_buf = NULL;

		static codegen_t cg;
		codegen_x64(&cg);
		
		int compile_status = compile_ast( root, &ctx, &cg );
		if ( !compile_status )
		{
            if ( (opt_flags & OPT_INSTR) != OPT_INSTR )
			{
				int build_elf_image( compiler_t * ctx, const char* binary_path );
				int build_elf64_image( compiler_t * ctx, const char* binary_path );
				int build_exe_image( compiler_t * ctx, const char* binary_path );
				int build_memory_image( compiler_t * ctx, const char* binary_path );
                int ret;
				switch (build_target)
				{
					case BT_WIN32:
						ret = build_exe_image(&ctx, dst);
						break;
					case BT_LINUX_X86:
						ret = build_elf_image(&ctx, dst);
						break;
					case BT_LINUX_X64:
						ret = build_elf64_image(&ctx, dst);
						break;
					case BT_MEMORY:
						ret = build_memory_image(&ctx, dst);
						break;
					case BT_OPCODES:
					{
						heap_string instr = ctx.instr;
						int n = heap_string_size(&instr);
						linked_list_reversed_foreach(ctx.relocations, struct relocation*, it,
						{
							if(it->type == RELOC_DATA)
							{
								*(u32*)&instr[it->from] = it->to + n;
							}
							else if(it->type == RELOC_CODE)
							{
								*(u32*)&instr[it->from] = it->to;
							} else
							{
								printf("unknown relocation type %d\n", it->type);
								exit(1);
							}
						});
						
						for(int i = 0; i < n; ++i)
						{
							printf("%02X%s", instr[i] & 0xff, i + 1 == n ? "" : " ");
						}
						putchar(' ');
						heap_string data_buf = ctx.data;
						size_t dl = heap_string_size(&data_buf);
						for(int i = 0; i < dl; ++i)
						{
							printf("%02X%s", data_buf[i] & 0xff, i + 1 == dl ? "" : " ");
						}
					} break;
				}
				if (opt_flags & OPT_VERBOSE)
				printf( "building image '%s' (return code = %d)\n", dst, ret );
			} else
			{
				for ( int i = 0; i < heap_string_size( &ctx.instr ); ++i )
					printf( "%02X ", ctx.instr[i] & 0xff );
                putchar('\n');
			}
			heap_string_free( &ctx.data );
			heap_string_free( &ctx.instr );
			linked_list_destroy( &ctx.relocations );
		}
		heap_string_free( &data_buf );
        
		root = NULL;
    	linked_list_destroy(&ast_list);
    }
    free(tokens);
	//getchar();
    return 0;
}
