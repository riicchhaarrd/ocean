int read(int fd, char *buf, int len)
{
    return syscall(3, fd, buf, len);
}

void write(int fd, const char *buf, int len)
{
    syscall(4, fd, buf, len);
}
