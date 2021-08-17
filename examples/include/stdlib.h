#ifndef STDLIB_H
#define STDLIB_H

#include <sys/syscall.h>
#include <stddef.h>

void exit(int code)
{
    syscall(SYS_exit, code);
}

int atoi(const char *_str)
{
    int total = 0;
    int len = 0;
    const char *str = _str;
    int neg = *str == '-';
    if(neg)
        ++str;
    const char *p = str;
    while(*p++) ++len;
    int exp = 1;
    for(int i = 0; i < len; ++i)
	{
        int t = str[len - i - 1] - '0';
        total += t * exp;
        exp *= 10;
	}
    if(neg)
        return -total;
	return total;
}

void *malloc(int size)
{
    int current = syscall(SYS_brk, 0);
    int next = syscall(SYS_brk, current + size);
    //printf("current=%x,next=%x\n",current,next);
    return current == next ? NULL : current;
}

void free(void *p)
{
    //does nothing atm
}
#endif
