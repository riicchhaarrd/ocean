#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main()
{
	while(1)
	{
        printf("time = %d\n", time(0));
		sleep( 1 );
	}
	return 0;
}
