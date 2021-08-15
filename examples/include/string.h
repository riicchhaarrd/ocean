#ifndef STRING_H
#define STRING_H

int strlen(const char *s)
{
    int len = 0;
    while(*s++) ++len;
    return len;
}

#endif
