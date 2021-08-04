#include <unistd.h>

void print(const char *s)
{
    write(1, s, strlen(s) + 1);
}
