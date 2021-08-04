#include <string.h>
#include <stdio.h>

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
