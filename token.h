#ifndef TOKEN_H
#define TOKEN_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

enum TOKEN_TYPE
{
    //ASCII characters reserved
    TK_IDENT = 256,
    TK_INTEGER,
    TK_STRING,
    TK_NUMBER,

    TK_PLUS_ASSIGN,
    TK_MINUS_ASSIGN,
    TK_MULTIPLY_ASSIGN,
    TK_DIVIDE_ASSIGN,
    
    TK_AND_ASSIGN,
    TK_OR_ASSIGN,
    TK_XOR_ASSIGN,
    TK_MOD_ASSIGN,

    TK_GEQUAL,
    TK_LEQUAL,

    TK_T_CHAR,
    TK_T_SHORT,
    TK_T_INT,
    TK_T_FLOAT,
    TK_T_DOUBLE,
    TK_T_NUMBER,
    TK_T_VOID,
    TK_T_UNSIGNED,

    TK_CONST,
    TK_SIZEOF,
    
    TK_LSHIFT,
    TK_RSHIFT,
    TK_EQUAL,
    TK_NOT_EQUAL,
    TK_IF,
    TK_FOR,
    TK_WHILE,
    TK_DO,
    TK_RETURN,
    TK_BREAK,
    TK_LOOP,
    TK_EMIT,
    TK_DOT_THREE_TIMES,
    TK_PLUS_PLUS,
    TK_MINUS_MINUS,
    
    TK_EOF,
    TK_MAX,
    TK_INVALID = -1
};

static const char *token_type_strings[TK_MAX] = {
	[TK_IDENT] = "ident",
    [TK_INTEGER] = "integer",
    [TK_STRING] = "string",
    [TK_NUMBER] = "number",

    
    [TK_LSHIFT] = "lshift",
    [TK_RSHIFT] = "rshift",
    [TK_EQUAL] = "equal",
    
    [TK_IF] = "if",
    [TK_FOR] = "for",
    [TK_LOOP] = "loop",
    [TK_EOF] = "eof"
};

static const int is_token_printable(int type)
{
    if(type >= 0x20 && type <= 0x7e)
        return 1;
    return 0;
}

//FIXME: not reentry safe, using static which may change
static const char *token_type_to_string(int type)
{
    static char buf[64] = {0};
    if(is_token_printable(type))
    {
        snprintf(buf, sizeof(buf), "%c", type);
        return buf;
    }
    return token_type_strings[type];
}

struct token
{
    int type;
    union
    {
        char string[32]; //C's max identifier length is 31 iirc
        float number;
        int integer;
        float vector[4];
    };
    int lineno;
    int curpos;
};


static void token_to_string(struct token *t, char *string, size_t n)
{
    assert(t != NULL);
    if(t->type == -1)
    {
        snprintf(string, n, "invalid");
        return;
    }
    switch(t->type)
    {
    case TK_IDENT:
    	snprintf(string, n, "type: %s, value: %s", token_type_strings[t->type], t->string);
        return;
    case TK_INTEGER:
        snprintf(string, n, "type: %s, value: %d", token_type_strings[t->type], t->integer);
        return;
    case TK_NUMBER:
        snprintf(string, n, "type: %s, value: %f", token_type_strings[t->type], t->number);
        return;
    }
}

#endif
