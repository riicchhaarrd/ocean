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

#endif
