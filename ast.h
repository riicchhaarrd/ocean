#ifndef AST_H
#define AST_H
#include <stdio.h>
#include <stdbool.h>
#include "data_type.h"
#include "arena.h"
#include "parse.h"

#define ENUM_BEGIN(typ) typedef enum {
#define ENUM(nam) nam
#define ENUM_VALUE(nam, val) nam = val
#define ENUM_END(typ) } typ;
#include "ast_node_type.h"

#include "types.h"

#undef ENUM_BEGIN
#undef ENUM
#undef ENUM_VALUE
#undef ENUM_END

#define ENUM_BEGIN(typ) static const char * typ ## _strings[] = {
#define ENUM(nam) #nam
#define ENUM_VALUE(nam, val) #nam
#define ENUM_END( typ )                                                                                                \
	}                                                                                                                  \
	;                                                                                                                  \
	static const char* typ##_to_string( int i )                                                                        \
	{                                                                                                                  \
		if ( i < 0 )                                                                                                   \
			return "invalid";                                                                                          \
		return typ##_strings[i];                                                                                       \
	}
#include "ast_node_type.h"

typedef struct ast_node_s ast_node_t;

#define IDENT_CHARLEN (64)

typedef enum
{
	LITERAL_INTEGER,
    LITERAL_NUMBER,
    LITERAL_STRING
} ast_literal_type_t;

typedef struct
{
    struct linked_list *body;
} ast_block_stmt_t;

typedef struct
{
    ast_literal_type_t type;

    union
    {
        char string[IDENT_CHARLEN]; //C's max identifier length is 31 iirc
        scalar_t scalar;
        integer_t integer;
        double vector[4];
    };
} ast_literal_t;

typedef struct
{
    char name[IDENT_CHARLEN];
} ast_identifier_t;

static void print_literal(ast_literal_t* lit)
{
    //TODO: FIX non-integers
    if(lit->type == LITERAL_INTEGER)
    printf("literal %lld\n", lit->integer.value);
    else if(lit->type == LITERAL_NUMBER)
    printf("literal %Lf\n", lit->scalar.value);
    else if(lit->type == LITERAL_STRING)
        printf("literal '%s'\n", lit->string);
    else
        printf("literal ??????\n");
}

typedef struct
{
    ast_node_t *lhs;
    ast_node_t *rhs;
    int operator;
} ast_bin_expr_t;

typedef struct
{
    ast_node_t *callee;
    ast_node_t *arguments[32];
    int numargs;
} ast_function_call_expr_t;

typedef struct
{
	ast_node_t *argument;
    int operator;
    int prefix;
} ast_unary_expr_t;

typedef struct
{
    ast_node_t *lhs;
    ast_node_t *rhs;
    int operator;
} ast_assignment_expr_t;

typedef struct
{
	ast_node_t *expr;
} ast_expr_stmt_t;

typedef struct
{
    ast_node_t *test;
    ast_node_t *consequent;
    ast_node_t *alternative;
} ast_if_stmt_t;

typedef struct
{
    ast_node_t *init;
    ast_node_t *test;
    ast_node_t *update;
    ast_node_t *body;
} ast_for_stmt_t;

typedef struct
{
    ast_node_t *test;
    ast_node_t *body;
} ast_while_stmt_t;

typedef struct
{
    ast_node_t *test;
    ast_node_t *body;
} ast_do_while_stmt_t;

typedef struct
{
    ast_node_t *id;
    ast_node_t *parameters[32];
    int numparms;
    ast_node_t *body; //no body means just forward declaration, just prototype function
    ast_node_t *return_data_type;
    int variadic;
    //TODO: access same named variables in different scopes
    ast_node_t *declarations[64]; //TODO: increase max amount of local variables, for now this'll do
    int numdeclarations;
} ast_function_decl_t;

typedef struct
{
    struct linked_list *body;   
} ast_program_t;

typedef struct
{
    ast_node_t *argument;
} ast_return_stmt_t;

typedef struct
{
    ast_node_t *object;
    ast_node_t *property;
    int computed; //unused atm
    int as_pointer;
} ast_member_expr_t;

enum TYPE_QUALIFIER
{
    TQ_NONE = 0,
    TQ_CONST = 1,
    TQ_VOLATILE = 2,
    TQ_UNSIGNED = 4
};

/* int,char,float,double etc...*/
typedef struct
{
    int primitive_type;
	int qualifiers;
} ast_primitive_t;

//TODO: FIXME rename
//maybe name is too generic?
typedef struct
{
    ast_node_t *data_type;
    int qualifiers;
	int array_size;
} ast_data_type_t;

typedef struct
{
	char name[IDENT_CHARLEN];
	ast_node_t* fields[32]; // TODO: increase N
	int numfields;
} ast_struct_decl_t;

typedef struct
{
    ast_node_t *id;
    ast_node_t *data_type;
    ast_node_t *initializer_value;
} ast_variable_decl_t;

typedef struct
{
    int opcode;
} ast_emit_t;

typedef struct
{
    ast_node_t *subject;
} ast_sizeof_t;

typedef struct
{
    ast_node_t *condition;
    ast_node_t *consequent;
    ast_node_t *alternative;
} ast_ternary_expr_t;

typedef struct
{
    //maybe add break level, nested loops
    //keep track of which loop node we're in maybe
    int unused;
} ast_break_stmt_t;

typedef struct
{
    ast_node_t *expr[16]; //TODO: increase N
    int numexpr;
} ast_seq_expr_t;

typedef struct
{
    ast_node_t *type;
    ast_node_t *expr;
} ast_cast_t;


//typedef node
//typedef unsigned char BYTE;

typedef struct
{
	char name[IDENT_CHARLEN];
    ast_node_t *type;
} ast_typedef_t;

// enum node
// enum colors { red, green, blue };

typedef struct
{
    char name[IDENT_CHARLEN]; //enum name
	ast_node_t* values[32]; //holds the identifiers (ast_identifier) the enum value is the index
	int numvalues;
} ast_enum_t;

typedef struct
{
    char ident[IDENT_CHARLEN];
    int value;
} ast_enum_value_t;

struct ast_node_s
{
	ast_node_t *parent;
	ast_node_type_t type;
	int start, end;
	int rvalue;
	union
	{
		ast_block_stmt_t block_stmt_data;
		ast_bin_expr_t bin_expr_data;
		ast_literal_t literal_data;
		ast_expr_stmt_t expr_stmt_data;
		ast_unary_expr_t unary_expr_data;
		ast_assignment_expr_t assignment_expr_data;
		ast_identifier_t identifier_data;
		ast_function_call_expr_t call_expr_data;
		ast_if_stmt_t if_stmt_data;
		ast_for_stmt_t for_stmt_data;
		ast_while_stmt_t while_stmt_data;
		ast_do_while_stmt_t do_while_stmt_data;
		ast_function_decl_t func_decl_data;
		ast_program_t program_data;
		ast_return_stmt_t return_stmt_data;
		ast_member_expr_t member_expr_data;
		ast_variable_decl_t variable_decl_data;
		ast_primitive_t primitive_data;
		ast_emit_t emit_data;
		ast_sizeof_t sizeof_data;
		ast_ternary_expr_t ternary_expr_data;
		ast_break_stmt_t break_stmt_data;
		ast_seq_expr_t seq_expr_data;
		ast_cast_t cast_data;
		ast_data_type_t data_type_data;
		ast_struct_decl_t struct_decl_data;
		ast_typedef_t typedef_data;
		ast_enum_t enum_data;
		ast_enum_value_t enum_value_data;
	};
};

static void ast_print_node_type(const char* key, ast_node_t* n)
{
	printf("node type: %s -> %s\n", key, ast_node_type_t_to_string(n->type));
}

struct ast_context
{
	arena_t *allocator;
	ast_node_t *program_node;
    ast_node_t *function;
    ast_node_t *default_function;
    struct hash_map *type_definitions;
	int numtypes;
	
    int verbose;
	
    struct parse_context parse_context;
    jmp_buf jmp;
};

typedef struct ast_context ast_context_t;
void ast_init_context(ast_context_t *ctx, arena_t *allocator);
bool ast_process_tokens(ast_context_t*, struct token* tokens, int num_tokens);

//TODO: refactor traverse_context name to ast

typedef int (*traversal_fn_t)(ast_node_t*, void*);

typedef struct
{
	jmp_buf jmp;
	traversal_fn_t visitor;
	void* userdata;
	size_t visiteestacksize;
	ast_node_t* visiteestack[8];
	int single_result;
	int overflow;
	ast_node_t **results;
	size_t maxresults, numresults;
} traverse_context_t;

ast_node_t* ast_tree_traverse(traverse_context_t* ctx, ast_node_t* head, traversal_fn_t visitor, void* userdata);
ast_node_t* ast_tree_node_by_type(traverse_context_t* ctx, ast_node_t* head, int type);
ast_node_t* ast_tree_node_by_identifier(traverse_context_t* ctx, ast_node_t* head, const char* id, int type);
ast_node_t* ast_tree_traverse_get_visitee(traverse_context_t* ctx, size_t index);
size_t ast_tree_nodes_by_type(traverse_context_t* ctx, ast_node_t* head, int type, ast_node_t** results, size_t maxresults);
ast_node_t* ast_tree_node_by_node(traverse_context_t* ctx, ast_node_t* head, ast_node_t* node);

#endif
