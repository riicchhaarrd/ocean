//TODO: write a preprocessor that includes stdio/string/stdlib so no need to paste utility functions in every file

int strlen(const char *s)
{
    int i = 0;
    while(s[i])
    {
        i+=1;
    }    
    return i;
}

//using syscall nr from x86 (32-bit)
void write(int fd, const char *buf, int len)
{
    syscall(4, fd, buf, len);
}

void print(const char *s)
{
    write(1, s, strlen(s) + 1);
}

int open(const char *filename, int flags, unsigned short mode)
{
    return syscall(5, filename, flags, mode);
}

void exit(int code)
{
    syscall(1, code);
}

int main()
{
    print("hello, world\n");
    
    //returns byte of time(0)
    exit(syscall(13));
}
