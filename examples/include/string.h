#ifndef STRING_H
#define STRING_H

int strlen(const char *s)
{
    int len = 0;
    while(*s++) len++;
    return len;
}

int strcpy(char *dst, char *src)
{
    //while(*dst++ = *src++);
    while(*src)
	{
        *dst = *src;
        dst++;
        src++;
	}
    *dst = *src;
	return 0;
}

int strcat(char *dst, char *src)
{
    int l = strlen(dst);
    strcpy(&dst[l], src);
    return 0;
}

int strcmp(const char *a, const char *b)
{
    int al = strlen(a);
    int bl = strlen(b);
    if(al != bl)
        return 1;
    for(int i = 0; i < al; ++i)
	{
        if(a[i] != b[i])
            return 1;
	}
    return 0;
}

void memset(char *p, int value, int n)
{
    for(int i = 0; i < n; ++i)
	{
        p[i] = value & 0xff;
	}
}

#endif
