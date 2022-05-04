#ifndef DATA_TYPE
#define DATA_TYPE
#include <stddef.h>

enum DATA_TYPE
{
    DT_CHAR,
    DT_SHORT,
    DT_INT,
    DT_FLOAT,
    DT_DOUBLE,
    DT_VOID,
    DT_LONG
};

static const char* data_type_strings[] = {
    "char", "short", "int", "float", "double", "void", "long", NULL
};

#endif
