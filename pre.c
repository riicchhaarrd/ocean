#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "token.h"
#include "parse.h"

#ifdef STANDALONE
#define HEAP_STRING_IMPL
#define LINKED_LIST_IMPL
#define HASH_MAP_IMPL
#endif

#include "rhd/heap_string.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"

//TODO: recursively including files, does not update filepath of the filename

//TODO: change default include path to be more dynamic
#define INCLUDE_PATH "examples/include/"

struct include_directive
{
    heap_string path;
    int start, end;
};

struct define_directive
{
    heap_string identifier;
    int function;
    heap_string parameters[32]; //TODO: increase amount?
    int numparameters;
    heap_string body;
};

struct pre_context
{
    struct parse_context parse_context;
    struct linked_list *includes;
    const char **includepaths;
    struct hash_map *identifiers;
    jmp_buf jmp;
    const char *sourcedir;
    heap_string data;
};

static int pre_accept(struct pre_context *ctx, int type)
{
    return parse_accept(&ctx->parse_context, type);
}

static struct token *pre_token(struct pre_context *ctx)
{
    return ctx->parse_context.current_token;
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
    assert(filename);
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

static void pre_expect(struct pre_context *ctx, int type)
{
    if(!pre_accept(ctx, type))
        return;
    printf("preprocessor error: expected token '%s', got '%s'\n", token_type_to_string(type), token_type_to_string(parse_token(&ctx->parse_context)->type));
    
    longjmp(ctx->jmp, 1);
}

static void pre_error(struct pre_context *ctx, const char *msg)
{
    printf("preprocess error: %s\n", msg);
    longjmp(ctx->jmp, 1);
}

static void append_token_buffer(struct pre_context *ctx, heap_string *str, struct token *tk)
{
    int len = tk->end - tk->start;
    for(int i = 0; i < len; ++i)
        heap_string_push(str, ctx->data[tk->start + i]);
}

static const char *pre_string(struct pre_context *ctx)
{
    if(!pre_token(ctx))
        return "";
    return pre_token(ctx)->string;
}

static struct define_directive *find_identifier(struct pre_context *ctx, const char *ident)
{
    return hash_map_find(ctx->identifiers, ident);
}

int file_exists( const char* filename )
{
	struct stat buffer;
	return ( stat( filename, &buffer ) == 0 );
}

heap_string locate_include_file(struct pre_context *ctx, const char *includepath)
{
    heap_string path = concatenate(ctx->sourcedir, includepath);
    if(file_exists(path))
        return path;
    heap_string_free(&path);

    for(const char **it = ctx->includepaths; *it; ++it)
	{
		path = concatenate( *it, includepath );
		if ( file_exists( path ) )
			return path;
		heap_string_free( &path );
	}
    return NULL;
}
heap_string preprocess_file(const char *filename, const char **includepaths, int verbose);
heap_string preprocess( struct pre_context* ctx , int verbose )
{

    heap_string preprocessed = NULL;
    
	while ( 1 )
	{
        int append = 1;
		struct token* tk = parse_advance( &ctx->parse_context );
		if ( !tk || tk->type == TK_EOF )
			break;

		switch ( tk->type )
		{
		case TK_IDENT:
		{
			struct define_directive* d = find_identifier( ctx, pre_string( ctx ) );
			if ( d )
            {
                //append = 0;
				//printf( "\tident: %s\n", pre_string( ctx ) );
            }
		}
		break;

		case '#':
			pre_expect( ctx, TK_IDENT );
			const char* directive = pre_string( ctx );
			if ( !strcmp( directive, "include" ) )
			{
                heap_string includepath = NULL;
                
				//printf( "got %s\n", pre_token( ctx )->string );
				struct token* n = parse_advance(&ctx->parse_context);
				if ( !pre_accept( ctx, '<' ) && !pre_accept( ctx, TK_STRING ) )
					pre_error( ctx, "expected < or string" );

				if ( n->type == '<' )
				{
					while ( 1 )
					{
						struct token* t = parse_token( &ctx->parse_context );
						if ( !t )
							pre_error( ctx, "unexpected eof" );
						if ( t->type == '>' )
						{
							parse_advance( &ctx->parse_context );
							break;
						}
						append_token_buffer( ctx, &includepath, t );
						// printf("tk type = %s (%s)\n", token_type_to_string(t->type), t->string);
						parse_advance( &ctx->parse_context );
					}
				}
				else
                {
					//printf("tk type = %s (%s)\n", token_type_to_string(n->type), n->string);
                    includepath = heap_string_new(n->string);
                }
                //printf("including '%s'\n", includepath);

                heap_string locatedincludepath = locate_include_file(ctx, includepath);
                heap_string includedata = preprocess_file(locatedincludepath, ctx->includepaths, verbose);
                heap_string_free(&locatedincludepath);
                //heap_string includedata = locate_and_read_include_file(ctx, includepath);
                if(!includedata)
                {
                    printf("failed to find include file '%s'\n", includepath);
                    heap_string_free( &includepath );
                    //pre_error(ctx, "include");
                    return NULL;
                }

				heap_string_append( &preprocessed, includedata );
				//heap_string_appendf(&preprocessed, "%s", includedata);
                heap_string_free(&includedata);
                
                append = 0;
                
				heap_string_free( &includepath );
			}
			else if ( !strcmp( directive, "define" ) )
			{
				pre_expect( ctx, TK_IDENT );
				const char* ident = pre_string( ctx );
				struct define_directive d = {
					.identifier = heap_string_new( ident ), .body = NULL, .function = 0, .numparameters = 0 };
				if ( !pre_accept( ctx, '(' ) )
				{
					d.function = 1;
					do
					{
						pre_expect( ctx, TK_IDENT );
						d.parameters[d.numparameters++] = heap_string_new( pre_string( ctx ) );
					} while ( !pre_accept( ctx, ',' ) );
					pre_expect( ctx, ')' );
				}
				hash_map_insert( ctx->identifiers, ident, d );
				// printf("defining %s, func = %d\n", ident, d.function);
			} else if(!strcmp(directive, "ifdef"))
            {
                pre_expect(ctx,TK_IDENT);
            } else if(!strcmp(directive, "endif"))
            {
            }
            append=0;
			break;
		}

        if(!append)
            continue;
		int l = tk->end - tk->start;
		assert( l > 0 );
		const char* buf = &ctx->data[tk->start];
		//printf( "%.*s", l, buf );
        //don't use appendf, has a hardcoded limit of 1024 at the time of writing this
        //heap_string_appendf(&preprocessed, "%.*s", l, buf);
        heap_string_appendn(&preprocessed, buf, l);
		// printf("tk type = %s (%s)\n", token_type_to_string(tk->type), tk->string);
	}
	return preprocessed;
}

heap_string preprocess_file(const char *filename, const char **includepaths, int verbose)
{
    int success = 1;
    heap_string result_data = NULL;
    heap_string data = heap_string_read_from_text_file(filename);
    if(!data)
        return NULL;
    heap_string dir = filepath(filename);
    struct pre_context ctx = {
        .includes = linked_list_create(struct include_directive),
        .identifiers = hash_map_create(struct define_directive),
        //TODO: FIXME add the source file that's including this file it's defines aswell / either through list or just copying the identifiers
        .data = data,
        .includepaths = includepaths,
        .sourcedir = dir
    };
    parse_initialize(&ctx.parse_context);
    parse_string(&ctx.parse_context, data, LEX_FL_NEWLINE_TOKEN | LEX_FL_BACKSLASH_TOKEN);
    if(setjmp(ctx.jmp))
    {
        printf("failed preprocessing file '%s'\n", filename);
        success = 0;
    }
    else
	{
		result_data = preprocess( &ctx, verbose );
        if(!result_data)
		{
			printf( "error, failed preprocessing\n" );
            success = 0;
		}
	}
	parse_cleanup(&ctx.parse_context);
    heap_string_free(&data);
    heap_string_free(&dir);
    return result_data;
}

#ifdef STANDALONE
int main(int argc, char **argv)
{
    int verbose = 0;
	assert( argc > 0 );
	//printf( "argc=%d\n", argc );
    const char *includepaths[16];
    int includepathindex = 0;
    //includepaths[includepathindex++] = "/usr/include/";
    //includepaths[includepathindex++] = "/usr/local/include/";
    includepaths[includepathindex++] = "examples/include/";
    includepaths[includepathindex] = NULL;
    
	int last_index = argc - 1;
	for ( int i = 1; i < last_index; ++i )
	{
		assert( argv[i][0] == '-' );
		switch ( argv[i][1] )
		{
        case 'v':
            verbose=1;
            break;
		case 'I':
		{
			const char* includepath = (const char*)&argv[i][2];
			if ( verbose )
				printf( "include path: %s\n", includepath );
			assert(includepathindex + 1 < 16);
            includepaths[includepathindex++] = includepath;
            includepaths[includepathindex] = NULL;
		}
		break;
		}
	}
	const char* source_filename = argv[last_index];
	if ( verbose )
	{
		printf( "src=%s\n", source_filename );
	}

	heap_string b = preprocess_file(argv[1], includepaths, verbose);
    if(b)
    printf("%s\n",b);
    if(b)
        heap_string_free(&b);
}
#endif
