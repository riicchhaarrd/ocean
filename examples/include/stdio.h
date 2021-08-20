#ifndef STDIO_H
#define STDIO_H

#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#define STDOUT_FILENO 1
#define STDIN_FILENO 0
#define STDERR_FILENO 2

void print(const char *s)
{
    write(STDOUT_FILENO, s, strlen(s) + 1);
}

int getchar()
{
    char buf[1];
    read(STDIN_FILENO, buf, 1);
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
    int neg = d < 0;
    if(neg)
        d = -d;
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
    if(neg)
	{
		buf[sizeof( buf ) - i - 2] = '-';
		++i;
	}
	print(&buf[sizeof(buf) - i - 1]);
}

void print_bits(int d, int little_endian)
{
    char buf[33];
    for(int i = 0; i < 32; ++i)
	{
		int b = d & ( 1 << i );
		buf[little_endian ? 32-i-1 : i] = b ? '1' : '0';
	}
	buf[32] = 0;
    print(buf);
	//write( STDOUT_FILENO, buf, sizeof(buf) );
}

int printf(const char *fmt, ...)
{
    va_list q;
    va_start(q, fmt);
    int l = strlen(fmt);
    for(int i = 0; i < l; ++i)
	{
        if(fmt[i] == '%')
		{
			int ch = fmt[i + 1];
			if(ch == 'd')
			{
				int argd = va_arg( q, int );
				print_decimal( argd );
			}
            //TODO: add elseif and switch statement
            if(ch == 's')
			{
                const char *args = va_arg(q, int); //TODO: fix preprocessor handle const char*
                print(args);
			}

            if(ch == 'x')
			{
				int argx = va_arg( q, int );
                print_hex(argx);
			}
			if ( ch == 'b' )
			{
				int argb = va_arg( q, int );
				print_bits( argb , 1);
			}
			if ( ch == 'B' )
			{
				int argb = va_arg( q, int );
				print_bits( argb , 0);
			}
			++i;
		}
		else
			putchar( fmt[i] );
	}
	va_end(q);
    return 0;
}
#endif
