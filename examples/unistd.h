void write(int fd, const char *buf, int len)
{
    syscall(4, fd, buf, len);
}
