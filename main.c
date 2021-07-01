#include <stdio.h>
#include <stdlib.h>

#include "token.h"
#include "ast.h"
#include "types.h"

#define HEAP_STRING_IMPL
#include "rhd/heap_string.h"

#define LINKED_LIST_IMPL
#include "rhd/linked_list.h"

heap_string read_file( const char* filename )
{
	heap_string data = NULL;

	FILE* fp = fopen( filename, "r" );
	if ( !fp )
		return data;
	fseek( fp, 0, SEEK_END );
	size_t fs = ftell( fp );
	rewind( fp );
	data = heap_string_alloc( fs );
	fread( data, fs, 1, fp );
	fclose( fp );
	return data;
}

// imported functions from other files
void parse(heap_string, struct token**, int*);
int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool);
heap_string x86(struct ast_node*);

int main( int argc, char** argv )
{
    if ( argc < 2 )
	    return 0;
    heap_string data = read_file( argv[1] );

    if ( !data )
    {
	    printf( "failed to read file '%s'\n", argv[1] );
	    return 1;
    }
    const char *mode = "";
    if(argc>2)
        mode = argv[2];
    
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
    int ast = generate_ast(tokens, num_tokens, &ast_list, &root, mode[0] != 'i');
    if(!ast)
    {
        if(mode[0] == 'i')
        {
            //generate native code
            heap_string instr = x86(root);
            if(instr)
            {
                //example rasm2 -a x86 -b 32 -d "B8 20 00 00 00 B9 08 00 00 00 F7 F9"
                //printf("x86 opcodes:\n");
                for(int i = 0; i < heap_string_size(&instr); ++i)
                    printf("%02X ", instr[i] & 0xff);
                heap_string_free(&instr);
            }
        }
    	linked_list_destroy(&root->program_data.body);
        root = NULL;
    	linked_list_destroy(&ast_list);
    }
    free(tokens);
    return 0;
}
