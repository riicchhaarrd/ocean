#include <stdio.h>
#include <stdlib.h>

#include "token.h"

#ifdef STANDALONE
#define HEAP_STRING_IMPL
#define LINKED_LIST_IMPL
#endif

#include "rhd/heap_string.h"
#include "rhd/linked_list.h"

//TODO: recursively including files, does not update filepath of the filename

struct include_directive
{
    heap_string path;
    int start, end;
};

struct parse_context
{
    int cursor;
    int buffer_size;
    const char *buffer;
    struct linked_list *includes;
};

static int read_character(struct parse_context *ctx)
{
    if(ctx->cursor >= ctx->buffer_size)
        return 0;
    return ctx->buffer[ctx->cursor++];
}

static heap_string read_till(struct parse_context *ctx, int character)
{
    heap_string hs = NULL;
    while(1)
	{
        int ch = read_character(ctx);
        if(!ch) break;
        if(ch == character)
		{
            --ctx->cursor;
            break;
		}
        heap_string_push(&hs, ch);
	}
	return hs;
}

static void seek_till(struct parse_context *ctx, int character)
{
    int ch;
    while( (ch = read_character(ctx)) && ch != character);
    --ctx->cursor;
}

static void skip(struct parse_context *ctx, int character)
{
    int ch;
    while( (ch = read_character(ctx)) && ch == character);
    --ctx->cursor;
}

static void advance(struct parse_context *ctx)
{
    ++ctx->cursor;
}

static int peek(struct parse_context *ctx)
{
    if(ctx->cursor >= ctx->buffer_size)
        return 0;
    return ctx->buffer[ctx->cursor];
}

static heap_string concatenate(const char *a, const char *b)
{
    if(!a)
        a = "";
    heap_string c = heap_string_alloc(strlen(a) + strlen(b) + 1);
    heap_string_appendf(&c, "%s%s", a, b);
    return c;
}

static heap_string filepath(const char* filename)
{
	heap_string fp = heap_string_new( filename );
	int sz = heap_string_size( &fp );
	for ( int i = 0; i < sz; ++i )
	{
		if ( fp[sz - i - 1] == '/' ) // path seperator
			break;
		fp[sz - i - 1] = 0;
	}
	return fp;
}

int preprocess_text( const char *text , heap_string *out_text, int *was_modified, const char *filename )
{
    *out_text = NULL;
    *was_modified = 0;
	struct parse_context ctx =
	{
        .cursor = 0,
        .buffer_size = strlen(text) + 1,
        .buffer = text
	};

    ctx.includes = linked_list_create(struct include_directive);

    while(1)
	{
        int start = ctx.cursor;
		int ch = read_character( &ctx );
        if(!ch) break;
        
        //exclude inside strings
        if(ch == '\'' || ch == '"')
		{
			seek_till( &ctx, ch );
			advance( &ctx );
		} else if(ch == '/' && peek(&ctx) == '/')
		{
			seek_till( &ctx, '\n' );
			advance( &ctx );
		} else if(ch == '/' && peek(&ctx) == '*')
        {
        search_asterisk:
            advance(&ctx);
            seek_till(&ctx, '*');
            advance(&ctx);
            if(peek(&ctx) && peek(&ctx) != '/') //TODO: add error: unterminated comment
                goto search_asterisk;
            advance(&ctx);
		} else if ( ch == '#' )
		{
			heap_string hs = read_till( &ctx, ' ' );
			if ( !strcmp( hs, "include" ) )
			{
                skip(&ctx, ' ');
                int rc = read_character(&ctx);
                assert(rc == '<' || rc == '"');
				//seek_till( &ctx, '"' );
				//advance( &ctx );
				heap_string arg = read_till( &ctx, rc == '<' ? '>' : rc );
				advance( &ctx );
				//printf( "hs=%s,arg=%s\n", hs, arg );
                
				struct include_directive directive = { .start = start, .end = ctx.cursor, .path = arg };
				linked_list_prepend( ctx.includes, directive );
				// heap_string_free( &arg );
			}

			heap_string_free( &hs );
		}
	}

    int pos = 0;
	heap_string nhs = NULL;
	linked_list_reversed_foreach( ctx.includes, struct include_directive*, it, {
		if ( it->start > pos )
			heap_string_appendn( &nhs, &text[pos], it->start - pos );

        heap_string data = NULL;

        if(strcmp(it->path, filename)) //only read the file and set data when the current file being processed isn't included recursively
		{
            heap_string fp = filepath(filename);
            heap_string fullpath = concatenate(fp, it->path);
            //printf("fullpath=%s\n",fullpath);
			data = heap_string_read_from_text_file( fullpath );
            heap_string_free(&fullpath);
            heap_string_free(&fp);
		} else
            ;//printf("error: circular include file '%s'\n", filename);

		if(!data)
		{
            //printf("error: failed to open file '%s'\n", it->path);
            heap_string_free(&nhs);
            goto ret;
		}
        
		heap_string_append( &nhs, data );
        heap_string_free(&data);
		pos = it->end;
        *was_modified = 1;
	} );

	//copy rest till eof
    if(ctx.buffer_size > pos)
        heap_string_appendn( &nhs, &text[pos], ctx.buffer_size - pos );

ret:
    //heap_string_free(&nhs);

	linked_list_reversed_foreach( ctx.includes, struct include_directive*, it, { heap_string_free( &it->path ); } );
	linked_list_destroy( &ctx.includes );
    *out_text = nhs;
    return 0;
}

heap_string preprocess_file(const char *filename)
{
	heap_string ping = heap_string_read_from_text_file( filename );

	if ( !ping )
	{
		printf( "failed to read file '%s'\n", filename );
		return NULL;
	}
	heap_string pong = NULL;
	int modified = 0;
    int pass=0;
	while(1)
	{
		preprocess_text( ping, &pong, &modified, filename );
        //printf("pass %d\n", pass++);
		// printf( "processed = %s\n", processed );
		heap_string_free( &ping );
        ping = pong;
        pong = NULL;
        if(!ping)
		{
            printf("error while preprocessing file '%s'\n", filename); //TODO: fix filename
            break;
		}
        if(!modified) break;
	}
	return ping;
}

#ifdef STANDALONE
int main(int argc, char **argv)
{
    assert(argc>0);
    heap_string b = preprocess_file(argv[1]);
    if(b)
    printf("%s\n",b);
    if(b)
        heap_string_free(&b);
}
#endif
