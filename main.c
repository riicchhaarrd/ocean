#include <stdio.h>
#include <stdlib.h>
#include "std.h"
#include "token.h"
#include "ast.h"
#include "types.h"

#define HEAP_STRING_IMPL
#include "rhd/heap_string.h"

#define LINKED_LIST_IMPL
#include "rhd/linked_list.h"

#define HASH_MAP_IMPL
#include "rhd/hash_map.h"

#include "compile.h"

// imported functions from other files
void parse(const char*, struct token**, int*);
int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool);
int x86(struct ast_node *head, struct compile_context *ctx);

int opt_flags = 0;

int main( int argc, char** argv )
{
    if ( argc < 2 )
	    return 0;
    /* pre.c */
    heap_string preprocess_file(const char *filename);
    heap_string data = preprocess_file( argv[1] );

    if ( !data )
    {
	    printf( "failed to read file '%s'\n", argv[1] );
	    return 1;
    }
    const char *mode = "";
    if(argc>2)
        mode = argv[2];

    for(int i = 1; i < argc; ++i)
	{
        if(argv[i][0] == '-')
		{
            switch(argv[i][1])
			{
            case 'v':
                opt_flags |= OPT_VERBOSE;
                break;
            case 'd':
                opt_flags |= OPT_DEBUG;
                break;
			}
		}
	}

	struct token *tokens = NULL;
    int num_tokens = 0;
    
	// printf("data = %s\n", data);
	parse( data , &tokens, &num_tokens);
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
    struct compile_context ctx;
    int ast = generate_ast(tokens, num_tokens, &ast_list, &root, mode[0] != 'i' && mode[0] != 'e');
    if(!ast)
    {
        if(mode[0] == 'i' || mode[0] == 'e')
        {
            //generate native code
            heap_string data_buf = NULL;
            int compile_status = x86(root, &ctx);
            if(!compile_status)
            {
                //example rasm2 -a x86 -b 32 -d "B8 20 00 00 00 B9 08 00 00 00 F7 F9"
                //printf("x86 opcodes:\n");
                for(int i = 0; i < heap_string_size(&ctx.instr); ++i)
                    printf("%02X ", ctx.instr[i] & 0xff);
				printf("\n");
				
				if(mode[0] == 'e')
				{
					//build elf
					int build_elf_image(struct compile_context* ctx, const char *binary_path);
					int ret = build_elf_image(&ctx, "bin/example.elf");
					printf("building elf image (return code = %d)\n", ret);
				}
    			heap_string_free(&ctx.data);
                heap_string_free(&ctx.instr);
    			linked_list_destroy(&ctx.relocations);
            }
            heap_string_free(&data_buf);
		}
        root = NULL;
    	linked_list_destroy(&ast_list);
    }
    free(tokens);
    return 0;
}
