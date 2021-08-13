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

// imported functions from other files
int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool);
int x86(struct ast_node *head, struct compile_context *ctx);

int opt_flags = 0;

int main( int argc, char** argv )
{
    assert(argc > 0);

    const char *files[argc];
    int numfiles = 0;
    
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
			case 'u':
			{
				const char* flag_str = (const char*)&argv[i][2];
				printf( "flag unused = %s\n", flag_str );
			}
			break;
			}
		} else
            files[numfiles++] = argv[i];
	}
    assert(numfiles > 1);
    //need atleast 1 source and 1 output file
    //TODO: multiple source files
	const char* src = files[numfiles - 2];
	const char* dst = files[numfiles - 1];
    //printf("src: %s, dst: %s\n", src, dst);
    int is_exe = strstr(dst, ".exe") != NULL;
    
    /* pre.c */
	heap_string preprocess_file( const char* filename, const char** includepaths, int verbose );
	const char* includepaths[] = { "examples/include/", NULL };
	heap_string data = preprocess_file( src, includepaths, 0 );

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
	struct compile_context ctx = { 0 };
    ctx.build_target = is_exe ? BT_WINDOWS : BT_LINUX;
	int ast = generate_ast(tokens, num_tokens, &ast_list, &root, opt_flags & OPT_AST);
    if(!ast && (opt_flags & OPT_AST) != OPT_AST)
    {
		// generate native code
		heap_string data_buf = NULL;
		int compile_status = x86( root, &ctx );
		if ( !compile_status )
		{
            if ( (opt_flags & OPT_INSTR) != OPT_INSTR )
			{
				int build_elf_image( struct compile_context * ctx, const char* binary_path );
				int build_exe_image( struct compile_context * ctx, const char* binary_path );
                int ret;

                if(is_exe)
                    ret = build_exe_image(&ctx, dst);
                else
                    ret = build_elf_image(&ctx, dst);
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
    return 0;
}
