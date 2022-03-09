#ifndef SYSCALL_H
#define SYSCALL_H

//TODO: switch between architectures with ifdef or such and #error on failure
//TODO: FIXME there's no guarantee that the function signature matches between architectures?

// x64

#define SYS_exit 60
#define SYS_fork 57
#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
//#define SYS_waitpid doesn't exist anymore
#define SYS_creat 85
#define SYS_link 86
#define SYS_unlink 87
#define SYS_execve 59
#define SYS_chdir 80
#define SYS_time 201
#define SYS_mknod 133
#define SYS_chmod 90
#define SYS_lchown 94

#define SYS_brk 12
#define SYS_nanosleep 35

// x86

//#define SYS_exit 1
//#define SYS_fork 2
//#define SYS_read 3
//#define SYS_write 4
//#define SYS_open 5
//#define SYS_close 6
//#define SYS_waitpid 7
//#define SYS_creat 8
//#define SYS_link 9
//#define SYS_unlink 10
//#define SYS_execve 11
//#define SYS_chdir 12
//#define SYS_time 13
//#define SYS_mknod 14
//#define SYS_chmod 15
//#define SYS_lchown 16
//
//#define SYS_brk 0x2d
//#define SYS_nanosleep 0xa2

//TODO: add more syscalls nr
#endif
