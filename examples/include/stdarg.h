#ifndef STDARG_H
#define STDARG_H

#define va_list void**
#define va_start(va, fmt) va = &fmt + sizeof(fmt)
#define va_arg(va, type) *va, va += sizeof(type)
#define va_end(va) va = 0

#endif
