#ifndef TIME_H
#define TIME_H

#include <sys/syscall.h>

int time(int timer)
{
    return syscall( SYS_time, timer );
}

#endif
