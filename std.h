#ifndef STD_H
#define STD_H

#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

enum OPT_FLAG
{
	OPT_VERBOSE,
    OPT_DEBUG
};

extern int opt_flags;

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
