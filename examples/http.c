#include <stdio.h>
#include <string.h>

struct sockaddr_in
{
    short sin_family;
    short sin_port;
    long s_addr;
    char sin_zero[8];
};

#define AF_INET 2
#define AF_INET6 10

#define SOCK_STREAM 1
#define SOCK_DGRAM 2

#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

//TODO: fix or,and,shl,shr for word size
int htons(int i)
{
    return ((i << 8) & 0xff00) | ((i >> 8) & 0xff);
}

int socket(int domain, int type, int protocol)
{
    return syscall(0x167, domain, type, protocol);
}

int accept(int sockfd, sockaddr_in *addr, int *addrlen)
{
    int flags = 0;
    return syscall(0x16c, sockfd, addr, addrlen, flags);
}

int bind(int sockfd, sockaddr_in *addr, int addrlen)
{
    return syscall(0x169, sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog)
{
    return syscall(0x16b, sockfd, backlog);
}

int close(int fd)
{
    return syscall(0x06, fd);
}

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

int shutdown(int fd, int how)
{
    return syscall(0x175, fd, how);
}

int main()
{
    int port = 8000;
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.s_addr = 0;
    sa.sin_port = htons(port);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == -1)
        return 0;
    printf("sock = %d\n", sock);
    int bnd = bind(sock, &sa, sizeof(sa));
    if(bnd == -1)
        return 0;
    printf("bnd = %d\n", bnd);

    listen(sock, 5);
	char reply[16384];
	reply[0] = 0;
    sprintf(reply, "HTTP/1.1 200 OK\r\n\r\nServer: http server\r\nContent-Type: text/html; charset=UTF-8\r\nhey");
    
	const char* html = "<h1>Hello world</h1><marquee>Test</marquee>";
	//strcat(reply, html);
    //print(reply);
    
	printf("reply = %s, len:%d\n", reply, strlen(reply));
	/* strcat(reply, "Content-Type: text/html\r\n"); */
	/* strcat(reply, "\r\n\r\n"); */

	/* strcat(reply, "Hello World"); */

	while(1)
	{
        sockaddr_in cl;
        int len = sizeof(cl);
        int fd = accept(sock, &cl, &len);
        printf("got client fd %d, len = %d\n", fd, len);

        if(fd == -1)
		{
            printf("failed to accept client\n");
            return 0;
		}

        
		write(fd, reply, strlen(reply));
        shutdown(fd, SHUT_WR);
        close(fd);
	}
	//printf("sizeof = %d\n",sizeof(sa));
    return 0;
}
