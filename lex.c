#include <stdio.h>
#include <stdlib.h>

#include "token.h"
#include "rhd/heap_string.h"
#include "rhd/linked_list.h"

struct lexer
{
    char *buf;
    int bufsz;
    int pos;
    struct token tk;
    int lineno;
    struct linked_list *tokens;
};

static int next(struct lexer *lex)
{
    if(lex->pos + 1 > lex->bufsz)
		return -1;
    return lex->buf[lex->pos++];
}

static int next_check(struct lexer *lex, int check)
{
    int pos = lex->pos;
    int n = next(lex);
    if(n != check)
    {
        lex->pos = pos;
        return 1;
    }
    return 0;
}

static heap_string next_match(struct lexer *lex, int (*cmp)(int))
{
    //undo the fetch from before
    --lex->pos;
    
    heap_string s = NULL;
    while(1)
    {
        int ch = next(lex);
        if(ch == -1 || !cmp(ch))
        {
            --lex->pos;
            return s;
        }
        heap_string_push(&s, ch);
    }
    return s;
}

static int match_test_ident(int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static int match_test_string(int ch)
{
    return ch != '"';
}

static int match_test_integer(int ch)
{
    return ch >= '0' && ch <= '9';
}

static int token(struct lexer *lex, struct token *tk)
{
    int ch;
retry:
    tk->type = TK_INVALID;
    ch = next(lex);
    if(ch == -1)
		return 1;
    if(ch == 0)
    {
		tk->type = TK_EOF;
        return 0;
    }
    switch(ch)
    {
	case ' ':
	case '\t':
	case '\r':
	case '\n':
	    goto retry;

	case '#':
	case '<':
        if(!next_check(lex, '<'))
        {
            tk->type = TK_LSHIFT;
            return 0;
        }
	case '>':
        if(!next_check(lex, '>'))
        {
            tk->type = TK_RSHIFT;
            return 0;
        }
	case '"':
    {
        ++lex->pos;
        tk->type = TK_STRING;
        tk->string[0] = 0;
        if(!next_check(lex, '"'))
        {
            return 0;
        }
        heap_string s = next_match(lex, match_test_string);
        snprintf(tk->string, sizeof(tk->string), "%s", s);
        heap_string_free(&s);
        if(next_check(lex, '"'))
        {
            //expected closing "
            return 1;
        }
    } break;

	case '\'':
	case '{':
	case '}':
	case '/':
	case '*':
	case '[':
	case ']':
	case '&':
	case '^':
	case '|':
	case '!':
	case '-':
	case '+':
	case '(':
	case ')':
	case '=':
        if(!next_check(lex, '='))
        {
            tk->type = TK_EQUAL;
            return 0;
        }
	case ';':
	case ':':
	case '\\':
	case ',':
	case '%':
	case '.':
	    tk->type = ch;
	    break;

	default:
	    if(match_test_integer(ch))
	    {
		tk->type = TK_INTEGER;
		heap_string s = next_match(lex, match_test_integer);
		tk->integer = atoi(s);
		heap_string_free(&s);
	    } else if(match_test_ident(ch))
	    {
		tk->type = TK_IDENT;
		heap_string s = next_match(lex, match_test_ident);
		//check whether this ident is a special ident
		if(!strcmp(s, "for"))
		    tk->type = TK_FOR;
		else if(!strcmp(s, "if"))
		    tk->type = TK_IF;
		snprintf(tk->string, sizeof(tk->string), "%s", s);
		heap_string_free(&s);
	    } else
	    {
		printf("got %c, unhandled error\n", ch);
		return 1; //error
	    }
	    break;
    }
    return 0;
}

void parse(heap_string data, struct token **tokens_out/*must be free'd*/, int *num_tokens)
{
    *tokens_out = NULL;
	*num_tokens = 0;
    
    int len = strlen(data);

    struct lexer lex = {
        .buf = data,
        .bufsz = strlen(data) + 1,
        .pos = 0,
        .lineno = 0,
        .tokens = NULL
    };

	lex.tokens = linked_list_create( struct token );

	struct token tk = { 0 };

	for ( int i = 0; i < len; ++i )
	{
		int ret = token( &lex, &tk );
		if ( ret )
		{
			break;
		}
		// if(tk.type == TK_IDENT)
		//printf("token = %s (%s)\n", token_type_to_string(tk.type), tk.string);
		linked_list_prepend( lex.tokens, tk );
        (*num_tokens)++;
	}

    //allocate num_tokens
    struct token *tokens = malloc(sizeof(struct token) * *num_tokens);
    assert(tokens != NULL);

    int index = 0;
	linked_list_reversed_foreach(lex.tokens, struct token*, it,
    {
	    if(it->type == TK_IDENT)
            ;//printf("]%s\n", it->string);

        memcpy(&tokens[index++], it, sizeof(struct token));
    });

    *tokens_out = tokens;

    linked_list_destroy(&lex.tokens);
}
