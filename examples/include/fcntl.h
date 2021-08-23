#ifndef FCNTL_H
#define FCNTL_H

#include <sys/syscall.h>

#define O_RDONLY (0)
#define O_WRONLY (1)
#define O_RDWR (2)

#define S_IRWXU (7 << 6)
#define S_IRUSR (4 << 6)
#define S_IWUSR (2 << 6)
#define S_IXUSR (1 << 6)

#define S_IRWXG (7 << 3)
#define S_IRGRP (4 << 3)
#define S_IXGRP (1 << 3)

#define S_IROTH (4)
#define S_IWOTH (2)
#define S_IXOTH (1)

#define S_ISUID (4 << 9)
#define S_ISGID (2 << 9)
#define S_ISVTX (1 << 9)

int open(const char *filename, int flags, int mode)
{
	return syscall(SYS_open, filename, flags, mode);
}

#endif
