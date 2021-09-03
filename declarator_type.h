#ifndef DECLARATOR_TYPE
#define DECLARATOR_TYPE

//references
//https://port70.net/~nsz/c/c11/n1570.html
//https://docs.microsoft.com/en-us/cpp/c-language/declarators-and-variable-declarations
//https://docs.microsoft.com/en-us/cpp/c-language/type-qualifiers
//https://docs.microsoft.com/en-us/cpp/c-language/overview-of-declarations

#define BIT(x) (1 << (x))

enum DATA_TYPE
{
    DT_CHAR,
    DT_UCHAR,
    DT_BOOL,
    DT_INT,
    DT_UINT,
    DT_SHORT,
    DT_USHORT,
    DT_LONG
};

enum TYPE_SPECIFIER
{
    TS_NONE,
    TS_VOID,
    TS_CHAR,
    TS_SHORT,
    TS_INT,
    TS_LONG,
    TS_FLOAT,
    TS_DOUBLE,
    TS_SIGNED,
    TS_UNSIGNED,
    TS_STRUCT,
    TS_UNION,
    TS_ENUM,
    TS_TYPEDEF_NAM
};

enum STORAGE_CLASS_SPECIFIER
{
    SCS_NONE = 0,
    SCS_TYPEDEF = BIT(0),
    SCS_EXTERN = BIT(1),
    SCS_STATIC = BIT(2),
    SCS_THREAD_LOCAL = BIT(3),
    SCS_AUTO = BIT(4),
    SCS_REGISTER = BIT(5)
};

enum TYPE_QUALIFIER
{
    TQ_NONE = 0,
    TQ_CONST = BIT(0),
    TQ_VOLATILE = BIT(1)
};

enum FUNCTION_SPECIFIER
{
    FS_NONE = 0,
    FS_INLINE = BIT(0),
    FS_NORETURN = BIT(1)
};

#endif
