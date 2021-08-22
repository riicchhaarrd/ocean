#include <stdio.h>
#include <stdlib.h>


void test(int *p)
{
    *(char*)p = 1;
}

int main()
{
    int a = 0xff << 8;
    printf("a = %d\n", a);
    test(&a);
    printf("a = %d\n", a);
    return 0;
}
