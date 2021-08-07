#include "parse.h"
#include "token.h"
#include "std.h"

struct token *parse_token(struct parse_context *ctx)
{
    if(ctx->token_index >= ctx->num_tokens)
        return NULL;
    return &ctx->tokens[ctx->token_index];
}

int parse_accept(struct parse_context *ctx, int type)
{
    struct token *tk = parse_token(ctx);
    if(!tk)
        return 1;
    if(tk->type != type)
    {
        //debug_printf("tk->type %s (%d) != type %s (%d)\n", token_type_to_string(tk->type), tk->type, token_type_to_string(type), type);
        //ctx->current_token = NULL;
        return 1;
    }
    ctx->current_token = tk;
	++ctx->token_index;
	return 0;
}
