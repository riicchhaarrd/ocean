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

    TK_LSHIFT,
    TK_RSHIFT,
    TK_EQUAL,
    TK_IF,
    TK_LOOP,
    TK_MAX,
    TK_INVALID = -1
};

static const char *token_type_strings[TK_MAX] = {
	[TK_IDENT] = "ident",
    [TK_INTEGER] = "integer",
    [TK_STRING] = "string",
    [TK_NUMBER] = "number",
    [TK_IF] = "if",
    [TK_LOOP] = "loop"
};

static const int is_token_printable(int type)
{
    if(type >= 0x20 && type <= 0x7e)
        return 1;
    return 0;
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
