#ifndef DATA_TYPE
#define DATA_TYPE

enum DATA_TYPE
{
    DT_CHAR,
    DT_SHORT,
    DT_INT,
    DT_FLOAT,
    DT_DOUBLE,
    DT_NUMBER,
    DT_VOID
};

static const char* data_type_strings[] = {
    "char", "short", "int", "float", "double", "number", "void", NULL
};

#endif
