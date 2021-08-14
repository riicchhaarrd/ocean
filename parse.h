#ifndef PARSE_H
#define PARSE_H
#include <setjmp.h>

struct parse_context
{
    struct token *tokens;
    int num_tokens;
    int token_index;
    struct token *current_token;

    jmp_buf jmp;
};

enum LEX_FLAG
{
    LEX_FL_NONE = 0,
    LEX_FL_NEWLINE_TOKEN = 1,
    LEX_FL_BACKSLASH_TOKEN = 2,
    LEX_FL_FORCE_IDENT = 4
    //LEX_FL_PREPROCESSOR_MODE = 4 //maybe
};

void parse(const char*, struct token**, int*, int);
int parse_accept( struct parse_context* ctx, int type );
struct token* parse_token( struct parse_context* ctx );
void parse_initialize( struct parse_context* ctx );
int parse_string( struct parse_context* ctx, const char* str, int );
void parse_cleanup( struct parse_context* ctx );
struct token* parse_advance( struct parse_context* ctx );
#endif
