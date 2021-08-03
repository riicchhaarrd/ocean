int strlen(const char *s)
{
    int i = 0;
    while(s[i])
    {
        i+=1;
    }    
    return i;
}

void print(const char *s)
{
    write(1, s, strlen(s) + 1);
}

int main()
{
    print("hello, world!\n");
    
    char msg[32];
    msg[0] = 'h';
    msg[1] = 'e';
    msg[2] = 'l';
    msg[3] = 'l';
    msg[4] = 'o';
    msg[5] = ',';
    msg[6] = ' ';
    msg[7] = 'w';
    msg[8] = 'o';
    msg[9] = 'r';
    msg[10] = 'l';
    msg[11] = 'd';
    msg[12] = '!';
    msg[13] = 10;
    msg[14] = 0;

    int i;
    
    for(i = 0; i < 10; i += 1)
    {
        if(i % 2 ==0)
        {
            print(msg);
        }
    }
}
