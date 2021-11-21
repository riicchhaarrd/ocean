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

#include "rhd/hash_string.h"

int generate_ast(struct token *tokens, int num_tokens, struct linked_list **ll/*for freeing the whole tree*/, struct ast_node **root, bool);
int main(int argc, char **argv)
{
	assert(argc > 1);
	
	//Step 1. Preprocess file first.
	/* pre.c */
	heap_string preprocess_file( const char* filename, const char** includepaths, int verbose, struct hash_map *defines, struct hash_map **defines_out);
	const char* includepaths[] = { "examples/include/", NULL };
	heap_string data = preprocess_file( argv[1], includepaths, 0, NULL, NULL );

	if ( !data )
    {
	    printf( "failed to read file '%s'\n", argv[1] );
	    return 1;
    }

	//Step 2. Tokenize the preprocessed result
	struct token *tokens = NULL;
    int num_tokens = 0;
    
	// printf("data = %s\n", data);
	parse( data , &tokens, &num_tokens, LEX_FL_NONE);
	heap_string_free( &data ); //We can now free the data because it's no longer needed, our tokens are contained in tokens.
    
	/*
	//Optionally print out the tokens.
    //printf("num_tokens = %d\n", num_tokens);
    char str[256]={0};
    for(int i = 0; i < num_tokens; ++i)
    {
        struct token *tk = &tokens[i];
        token_to_string(tk, str, sizeof(str));
		//printf("token %s\n", str);
    }
	*/

    struct linked_list *ast_list = NULL;
    struct ast_node *root = NULL;

	//Step 3. Generate AST from tokens.
	int ast = generate_ast(tokens, num_tokens, &ast_list, &root, 1);
	if(ast)
	{
		printf("Failed to generate AST\n");
		return 0;
	}
	root = NULL;
	linked_list_destroy(&ast_list);
    free(tokens);
	return 0;
}