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

//DONE
///* */ style comments
//post/pre increment
//if() without brackets matching till ;

//TODO
//|| and && expressions
//preprocessor #define e.g for EOF (-1)
//'\n' character literals
//https://en.cppreference.com/w/cpp/language/statements
//https://stackoverflow.com/questions/51886189/can-c-have-code-in-the-global-scope
//add global declarations/definitions fix functions in ast https://stackoverflow.com/questions/1410563/what-is-the-difference-between-a-definition-and-a-declaration

char *gets(char *str)
{
    int i = 0;
    while(1)
	{
		int c = getchar();
        if(c == -1) //TODO: replace with EOF
		{
			break;
		}
		if(c == 20)
		{
			break;
		}
		str[i] = c;
        i += 1;
	}
}

int printf(const char *s, ...)
{
}
