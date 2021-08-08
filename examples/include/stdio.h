#include <unistd.h>
#include <string.h>

void print(const char *s)
{
    write(1, s, strlen(s) + 1);
}

int getchar()
{
    char buf[1];
    read(0, buf, 1);
    return buf[0];
}

void putchar(int ch)
{
    char buf[2];
    buf[0] = ch;
    buf[1] = 0;
    print(buf);
}

void print_hex(int d)
{
    char buf[32];
    int i = 0;
    buf[31] = 0;
    while(d > 0)
	{
        int m = d % 16;
        if(m < 10)
        buf[sizeof(buf) - i - 2] = m + '0';
        if(m >= 10)
            buf[sizeof(buf) - i - 2] = (m-10) + 'A';
        d /= 16;
        i += 1;
	}
    print(&buf[sizeof(buf) - i - 1]);
}

void print_decimal(int d)
{
    char buf[32];
    int i = 0;
    buf[31] = 0;
    do
	{
        int m = d % 10;
        buf[sizeof(buf) - i - 2] = m + '0';
        d /= 10;
        i += 1;
	} while(d > 0);
    print(&buf[sizeof(buf) - i - 1]);
}

int printf(const char *s, ...)
{
}
