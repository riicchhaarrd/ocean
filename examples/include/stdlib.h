void exit(int code)
{
    syscall(1, code);
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
