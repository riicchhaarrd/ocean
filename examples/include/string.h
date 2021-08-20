#ifndef STRING_H
#define STRING_H

int strlen(const char *s)
{
    int len = 0;
    while(*s++) len++;
    return len;
}

void memset(char *p, int value, int n)
{
    for(int i = 0; i < n; ++i)
	{
        p[i] = value & 0xff;
	}
}

#endif
