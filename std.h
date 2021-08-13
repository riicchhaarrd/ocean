#ifndef STD_H
#define STD_H

#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

enum OPT_FLAG
{
	OPT_VERBOSE = 1,
    OPT_DEBUG = 2,
    OPT_AST = 4,
    OPT_INSTR = 8
};

extern int opt_flags;

static int debug_printf_r(int lineno, const char *filename, const char *fmt, ...)
{
	char buffer[512] = { 0 };
    va_list va;
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    printf("[%s:%d] %s", filename, lineno, buffer);
    va_end(va);
    return 0;
}

#define debug_printf(fmt, ...) debug_printf_r(__LINE__, __FILE__, fmt, ## __VA_ARGS__)
#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof((x)) / sizeof((x)[0]))
#endif
static void FIXME_FN( const char* filename, int linenumber, const char* fmt, ... )
{
    //TODO: FIXME unsafe reentry etc
	char buffer[512] = { 0 };
    int n = snprintf(buffer, sizeof(buffer), "[fixme:%s:%d] ", filename, linenumber);
    assert(n < sizeof(buffer));
    
	va_list args;
	va_start( args, fmt );
	vsnprintf( &buffer[n], sizeof( buffer ) - n, fmt, args );
	//perror( buffer );
    printf("%s", buffer);
	va_end( args );
}

#define FIXME( fmt, ... ) \
    do { \
    	FIXME_FN( __FILE__, __LINE__, fmt, ## __VA_ARGS__ );   \
    } while(0)

#endif
