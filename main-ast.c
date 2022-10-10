#define HEAP_STRING_IMPL
#include "rhd/heap_string.h"

#define LINKED_LIST_IMPL
#include "rhd/linked_list.h"

#define HASH_MAP_IMPL
#include "rhd/hash_map.h"

#include "rhd/hash_string.h"

#include "arena.h"
#include "std.h"
#include "token.h"
#include "ast.h"
#include "types.h"
#include "parse.h"
#include "compile.h"

static void print_hex(u8 *buf, size_t n)
{
	for (int i = 0; i < n; ++i)
	{
		printf("%02X%s", buf[i] & 0xff, i + 1 == n ? "" : " ");
	}
}

int generate_ast(struct token* tokens, int num_tokens, struct linked_list** ll /*for freeing the whole tree*/,
				 struct ast_node** root, bool);
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
	struct token* tokens = NULL;
	int num_tokens = 0;
    
	// printf("data = %s\n", data);
	parse( data , &tokens, &num_tokens, LEX_FL_NONE);

	
	//Optionally print out the tokens.
	/* char str[256]={0}; */
	/* for(int i = 0; i < num_tokens; ++i) */
	/* { */
	/* 	struct token *tk = &tokens[i]; */
	/* 	token_stringify(data, heap_string_size(&data), tk, str, sizeof(str)); */
	/* 	printf("%s", str); */
	/* } */

	arena_t* arena;
	arena_create(&arena, "ast", 1000 * 1000 * 128); // 128MB
	
	ast_context_t ast_context;
	ast_init_context(&ast_context, arena);

	if(ast_process_tokens(&ast_context, tokens, num_tokens))
	{
		/* print_ast(ast_context.program_node, 0); */
		/* printf("done processing tokens\n"); */
	}
	// gcc -w -g main-ast.c lex.c ast.c pre.c parse.c && gdb -ex run --args ./a.out examples/syscall.c

	/* struct linked_list *ast_list = NULL; */
    /* struct ast_node *root = NULL; */

	/* //Step 3. Generate AST from tokens. */
	/* int ast = generate_ast(tokens, num_tokens, &ast_list, &root, 1); */
	/* if(ast) */
	/* { */
	/* 	printf("Failed to generate AST\n"); */
	/* 	return 0; */
	/* } */
	/* root = NULL; */
	/* linked_list_destroy(&ast_list); */

	compiler_t compile_ctx;
	compiler_init(&compile_ctx, arena, 64, COMPILER_FLAGS_NONE);
	int compile(compiler_t * ctx, ast_node_t * head);
	compile(&compile_ctx, ast_context.program_node);

	function_t* lookup_function_by_name(compiler_t* ctx, const char* name);
	function_t *fn = lookup_function_by_name(&compile_ctx, "main");
	assert(fn);
	heap_string s = NULL;
	bool x86(function_t * f, heap_string*);
	/* x86(fn, &s); */
	/* print_hex(s, heap_string_size(&s)); */
	
	heap_string_free(&s);
	free(tokens);
	heap_string_free(&data);
	arena_destroy(&arena);
	return 0;
}
