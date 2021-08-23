#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

void test()
{
	char buf[32];
	int fd = open("pre.c", 0, 0);
	printf("fd = %x\n", fd);
	int n = 0;
	do
	{
		n = read(fd, buf, sizeof(buf));
		// printf("n = %d\n", n);
		// printf("buf = %s\n", buf);
		for (int i = 0; i < sizeof(buf); ++i)
			putchar(buf[i]);
	} while (n == sizeof(buf));
}

int main()
{
	test();
	return 0;
}
