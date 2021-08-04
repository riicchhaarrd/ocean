int strlen(const char *s)
{
    int i = 0;
    while(s[i])
    {
        i+=1;
    }    
    return i;
}

void write(int fd, const char *buf, int len)
{
    syscall(4, fd, buf, len);
}

void print(const char *s)
{
    write(1, s, strlen(s) + 1);
}

int main()
{
    int i = 1;
    
    while(i < 10)
    {
        print("while\n");
        if(i % 5 == 0)
        {
            print("breaking while\n");
            break;
        }
        i += 1;
    }
    
    for(i = 1; i < 10; i += 1)
    {
        print("for\n");
        if(i % 5 == 0)
        {
            print("breaking for\n");
            break;
        }
    }
    return 0;
}
