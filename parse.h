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

int parse_accept(struct parse_context *ctx, int type);
struct token *parse_token(struct parse_context *ctx);
#endif
