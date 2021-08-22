#ifndef UNISTD_H
#define UNISTD_H

int read(int fd, char *buf, int len)
{
    return syscall(SYS_read, fd, buf, len);
}

void write(int fd, const char *buf, int len)
{
    syscall(SYS_write, fd, buf, len);
}

int sleep(int sec)
{
    int d[2];
    d[0] = sec;
    d[1] = 0;

    int ret = syscall(SYS_nanosleep, d, 0);
    //TODO: handle EINTR
    if(ret < 0)
	{
        int errno = -ret;
		//printf( "sleep failed, ret = %x %d\n", errno, errno);
        return sec;
	}
    return 0;
}

#endif
