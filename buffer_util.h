#ifndef BUFFER_UTIL_H
#define BUFFER_UTIL_H

static int instruction_position(compiler_t *ctx)
{
    return heap_string_size(&ctx->function->bytecode);
}

static void dd(compiler_t *ctx, u32 i)
{
    union
    {
        uint32_t i;
        uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
		heap_string_push(&ctx->function->bytecode, u.b[i]);
}

static void dw(compiler_t *ctx, u16 i)
{
    union
    {
        uint16_t s;
        uint8_t b[2];
    } u = { .s = i };

    heap_string_push(&ctx->function->bytecode, u.b[0]);
    heap_string_push(&ctx->function->bytecode, u.b[1]);
}

static void db(compiler_t *ctx, u8 op)
{
    heap_string_push(&ctx->function->bytecode, op);
}

static void set8(compiler_t *ctx, int offset, u8 op)
{
    ctx->function->bytecode[offset] = op;
}

static void set32(compiler_t *ctx, int offset, u32 value)
{
    u32 *ptr = (u32*)&ctx->function->bytecode[offset];
    *ptr = value;
}

static void buf(compiler_t *ctx, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(&ctx->function->bytecode, buf[i] & 0xff);
    }
}
#endif