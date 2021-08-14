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
    int verbose;
    int scope_bit;
    int scope_visibility;
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

static void handle_define_ident( struct pre_context* ctx, struct define_directive* d, heap_string* preprocessed )
{
	if ( !pre_accept( ctx, '(' ) )
	{
        assert(d->function);

		int nargs = 0;
		struct token *args[16];

        do
        {
            struct token *tk = parse_advance(&ctx->parse_context);
            if(!tk || tk->type == TK_EOF)
                return;

            if(tk->type != TK_INTEGER && tk->type != TK_STRING && tk->type != TK_IDENT)
            {
                //TODO: FIXME handle actual expressions and nested macro functions
                //for now this is good enough for my usecase
				pre_error( ctx, "expected string, ident or integer" );
                break;
            }
            args[nargs++] = tk;
		} while ( !pre_accept( ctx, ',' ) );
		pre_expect( ctx, ')' );

		struct parse_context tmp;
		parse_initialize( &tmp );
		parse_string( &tmp, d->body, LEX_FL_NEWLINE_TOKEN | LEX_FL_BACKSLASH_TOKEN | LEX_FL_FORCE_IDENT );
		while ( 1 )
		{
			struct token* dt = parse_advance( &tmp );
			if ( !dt || dt->type == TK_EOF )
				break;
			//printf( "tk = %.*s\n", dl, dbuf );
            switch(dt->type)
			{
			case TK_IDENT:
			{
				int param_index = -1;
				for ( int i = 0; i < d->numparameters; ++i )
				{
					if ( !strcmp( d->parameters[i], dt->string ) )
					{
						param_index = i;
						break;
					}
				}
				if ( param_index != -1 )
				{
					//printf( "%s is at %d\n", dt->string, param_index );
					//printf( "replace = %d\n", args[param_index]->integer );
                    struct token *parm_token = args[param_index];
					int dl = parm_token->end - parm_token->start;
					assert( dl > 0 );
					const char* dbuf = &ctx->data[parm_token->start];
                    //TODO: FIXME should we push ' ' by hand?
                    heap_string_push(preprocessed, ' '); //incase no space for ident
					heap_string_appendn( preprocessed, dbuf, dl );
                    break;
				}
			}

			default:
			{
				int dl = dt->end - dt->start;
				assert( dl > 0 );
				const char* dbuf = &d->body[dt->start];
				//printf( "dt type=%s,%d\n", token_type_to_string( dt->type ), dt->type );
				heap_string_appendn( preprocessed, dbuf, dl );
			}
			break;
			}
		}
		parse_cleanup( &tmp );
		// pre_expect(ctx, ')');
	}
	else
	{
		heap_string_append( preprocessed, d->body );
	}
}

heap_string preprocess_file(const char *filename, const char **includepaths, int verbose);
static int handle_token( struct pre_context *ctx, heap_string* preprocessed, struct token* tk, int *handled )
{
    *handled = 0;
	switch ( tk->type )
	{
	case TK_IDENT:
	{
		struct define_directive* d = find_identifier( ctx, pre_string( ctx ) );
		if ( d )
		{
			handle_define_ident( ctx, d, preprocessed );
		}
		else
		{
            //ident not handled/replaced, return 0 and original ident buffer will be appended to the preprocessed buffer
            return 0;
        }
	}
	break;

	case '#':
		pre_expect( ctx, TK_IDENT );
		const char* directive = pre_string( ctx );
		if ( !strcmp( directive, "include" ) )
		{
			heap_string includepath = NULL;

			// printf( "got %s\n", pre_token( ctx )->string );
			struct token* n = parse_advance( &ctx->parse_context );
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
				// printf("tk type = %s (%s)\n", token_type_to_string(n->type), n->string);
				includepath = heap_string_new( n->string );
			}
			// printf("including '%s'\n", includepath);

			heap_string locatedincludepath = locate_include_file( ctx, includepath );
			heap_string includedata = preprocess_file( locatedincludepath, ctx->includepaths, ctx->verbose );
			heap_string_free( &locatedincludepath );
			// heap_string includedata = locate_and_read_include_file(ctx, includepath);
			if ( !includedata )
			{
				printf( "failed to find include file '%s'\n", includepath );
				heap_string_free( &includepath );
				// pre_error(ctx, "include");
				return 1;
			}

			heap_string_append( preprocessed, includedata );
			// heap_string_appendf(preprocessed, "%s", includedata);
			heap_string_free( &includedata );
			heap_string_free( &includepath );
		}
		else if ( !strcmp( directive, "define" ) )
		{
			pre_expect( ctx, TK_IDENT );
			int ident_end = pre_token( ctx )->end;
			const char* ident = pre_string( ctx );
			struct define_directive d = {
				.identifier = heap_string_new( ident ), .body = NULL, .function = 0, .numparameters = 0 };

			if ( ctx->data[ident_end] == '(' )
			{
				parse_advance( &ctx->parse_context );
				d.function = 1;
				do
				{
					pre_expect( ctx, TK_IDENT );
					d.parameters[d.numparameters++] = heap_string_new( pre_string( ctx ) );
				} while ( !pre_accept( ctx, ',' ) );
				pre_expect( ctx, ')' );
			}
			int bs = 0;
			while ( 1 )
			{
				struct token* t = parse_token( &ctx->parse_context );
				if ( !t )
					pre_error( ctx, "unexpected eof" );
				if ( t->type == '\n' )
				{
					if ( !bs )
					{
						parse_advance( &ctx->parse_context );
						break;
					}
					bs = 0;
				}
				if ( t->type == '\\' )
					bs = 1;
				else
					append_token_buffer( ctx, &d.body, t );
				// TODO: FIXME free body
				// printf("tk type = %s (%s)\n", token_type_to_string(t->type), t->string);
				parse_advance( &ctx->parse_context );
			}
			hash_map_insert( ctx->identifiers, ident, d );
			// printf("defining %s, func = %d\n", ident, d.function);
		}
		else if ( !strcmp( directive, "ifndef" ) )
		{
			pre_expect( ctx, TK_IDENT );
			int expr = find_identifier( ctx, pre_string(ctx) ) == NULL ? 1 : 0;
			// heap_string_appendf(preprocessed, "// expr = %d\n", expr);
			++ctx->scope_bit;
			ctx->scope_visibility |= ( expr << ctx->scope_bit );
		}
		else if ( !strcmp( directive, "ifdef" ) )
		{
			pre_expect( ctx, TK_IDENT );
			int expr = find_identifier( ctx, pre_string(ctx) ) == NULL ? 0 : 1;
			// heap_string_appendf(preprocessed, "// expr = %d\n", expr);
			++ctx->scope_bit;
			ctx->scope_visibility |= ( expr << ctx->scope_bit );
		}
		else if ( !strcmp( directive, "if" ) )
		{
			struct token* n = parse_advance( &ctx->parse_context );
			// TODO: FIXME make #if work with expressions
			if ( !pre_accept( ctx, TK_INTEGER ) && !pre_accept( ctx, TK_IDENT ) )
				pre_error( ctx, "expected integer or ident" );
			int expr = ( n->type == TK_INTEGER ? n->integer : ( find_identifier( ctx, n->string ) != NULL ) ) != 0;
			// heap_string_appendf(preprocessed, "// expr = %d\n", expr);
			++ctx->scope_bit;
			ctx->scope_visibility |= ( expr << ctx->scope_bit );
		}
		else if ( !strcmp( directive, "undef" ) )
		{
			pre_expect( ctx, TK_IDENT );
			hash_map_remove_key( &ctx->identifiers, pre_string( ctx ) );
		}
		break;

    default:
        *handled = 0;
        return 0;
	}
    *handled = 1;
    return 0;
}

heap_string preprocess( struct pre_context* ctx )
{

    heap_string preprocessed = NULL;
    ctx->scope_bit = 0;
    ctx->scope_visibility = 1; //TODO: FIXME: max scopes
    
	while ( 1 )
	{
		struct token* tk = parse_advance( &ctx->parse_context );
		if ( !tk || tk->type == TK_EOF )
			break;
        if(tk->type == TK_IDENT && !strcmp(tk->string, "endif"))
        {
            assert(ctx->scope_bit > 0);
			--ctx->scope_bit;
            continue;
        }
		int in_scope = ( ctx->scope_visibility & ( 1 << ctx->scope_bit ) );
		if ( !in_scope )
			continue;
		int handled;
		int err = handle_token( ctx, &preprocessed, tk, &handled );
		if ( err )
			return NULL;
		if ( handled )
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
        .sourcedir = dir,
        .verbose = verbose
    };
    parse_initialize(&ctx.parse_context);
    parse_string(&ctx.parse_context, data, LEX_FL_NEWLINE_TOKEN | LEX_FL_BACKSLASH_TOKEN | LEX_FL_FORCE_IDENT);
    if(setjmp(ctx.jmp))
    {
        printf("failed preprocessing file '%s'\n", filename);
        success = 0;
    }
    else
	{
		result_data = preprocess( &ctx );
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
