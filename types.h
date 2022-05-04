#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef enum
{
	INTEGER_SUFFIX_NONE,
	INTEGER_SUFFIX_LONG,
	INTEGER_SUFFIX_LONG_LONG,
	INTEGER_SUFFIX_SIZE
} integer_suffix_t;

typedef struct
{
	union
	{
		i64 value;
		u64 unsigned_value;
	};
	bool is_unsigned;
	integer_suffix_t suffix;

} integer_t;

typedef enum
{
	SCALAR_SUFFIX_NONE,
	SCALAR_SUFFIX_FLOAT,
	SCALAR_SUFFIX_LONG_DOUBLE
} scalar_suffix_t;

typedef struct
{
	long double value;
	scalar_suffix_t suffix;
} scalar_t;

#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

#endif
