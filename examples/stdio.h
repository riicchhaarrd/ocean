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

void print_decimal(int d)
{
    char buf[32];
    int i = 0;
    buf[31] = 0;
    while(d > 0)
	{
        int m = d % 10;
        buf[sizeof(buf) - i - 2] = m + '0';
        d /= 10;
        i += 1;
	}
    print(&buf[sizeof(buf) - i - 1]);
}
