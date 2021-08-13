#include "parse.h"
#include "token.h"
#include "std.h"

struct token *parse_token(struct parse_context *ctx)
{
    if(ctx->token_index >= ctx->num_tokens)
        return NULL;
    ctx->current_token = &ctx->tokens[ctx->token_index];
    return ctx->current_token;
}

struct token *parse_advance(struct parse_context *ctx)
{
    struct token *t = parse_token(ctx);
    ++ctx->token_index;
    return t;
}

void parse_initialize(struct parse_context *ctx)
{
    ctx->current_token = NULL;
    ctx->num_tokens = 0;
    ctx->token_index = 0;
    ctx->tokens = NULL;
}

int parse_string(struct parse_context *ctx, const char *str, int flags)
{
    //TODO: handle errors
    parse(str, &ctx->tokens, &ctx->num_tokens, flags);
    return 0;
}

void parse_cleanup(struct parse_context *ctx)
{
    free(ctx->tokens);
}

int parse_accept(struct parse_context *ctx, int type)
{
    struct token *old_token = ctx->current_token;
    struct token *tk = parse_token(ctx);
    
    if(!tk || tk->type != type)
	{
		ctx->current_token = old_token;
		//debug_printf("tk->type %s (%d) != type %s (%d)\n", token_type_to_string(tk->type), tk->type, token_type_to_string(type), type);
        return 1;
	}
    ++ctx->token_index;
	return 0;
}
