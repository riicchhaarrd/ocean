#ifndef UTIL_H
#define UTIL_H
#include "rhd/heap_string.h"
#include <stdint.h>
#include "types.h"

static int dd(heap_string *s, uint32_t i)
{
	int sz = heap_string_size( s );
	union
	{
		uint32_t i;
		uint8_t b[4];
	} u = { .i = i };

	for ( size_t i = 0; i < 4; ++i )
		heap_string_push( s, u.b[i] );

	return sz;
}

static int dq(heap_string *s, uint64_t i)
{
	int sz = heap_string_size( s );
	union
	{
		uint64_t i;
		uint8_t b[8];
	} u = { .i = i };

	for ( size_t i = 0; i < 8; ++i )
		heap_string_push( s, u.b[i] );

	return sz;
}

static int dw( heap_string* s, uint16_t i )
{
	int sz = heap_string_size( s );
	union
	{
		uint16_t s;
		uint8_t b[2];
	} u = { .s = i };

	heap_string_push( s, u.b[0] );
	heap_string_push( s, u.b[1] );
	return sz;
}

static int db(heap_string *s, u8 op)
{
    heap_string_push(s, op);
    return heap_string_size(s) - 1;
}

static void pad(heap_string *s, u32 n)
{
    for(int i = 0; i < n; ++i)
        heap_string_push(s, 0x0);
}

static int align_to(int pos, int align)
{
    if(pos % align == 0)
        return pos; //no alignment needed
    return align - (pos % align);
}

static void pad_align(heap_string *s, int align)
{
    int pos = heap_string_size(s);
    if(pos % align == 0)
        return; //no alignment needed
    int m = align - (pos % align);
    for(int i = 0; i < m; ++i)
        heap_string_push(s, 0);
}

static void buf(heap_string *s, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(s, buf[i] & 0xff);
    }
}

#endif
