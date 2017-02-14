/* Minimalistic macro definitions and declarations, subset of uClibc. */

#ifndef _MINIINC_H_
#define _MINIINC_H_ 1

#ifndef __linux__
#error Linux is needed.
#endif
#ifndef __i386__
#error i386 is needed.  /* This fails on amd64 (__amd64__, __x86_64__). Good. */
#endif

#include <stddef.h>  /* This is a compiler-specific #include. */

#define __WAIT_INT(status)    (*(__const int *) &(status))
#define __WTERMSIG(status)    ((status) & 0x7f)
#define __WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define __WIFEXITED(status)   (__WTERMSIG(status) == 0)
#define WIFEXITED(status)     __WIFEXITED(__WAIT_INT(status))
#define WEXITSTATUS(status)   __WEXITSTATUS(__WAIT_INT(status))

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)

__extension__ typedef unsigned long long int __u_quad_t;
__extension__ typedef __u_quad_t __dev_t;
__extension__ typedef unsigned int __uid_t;
__extension__ typedef unsigned int __gid_t;
__extension__ typedef unsigned long int __ino_t;
__extension__ typedef unsigned int __mode_t;
__extension__ typedef unsigned int __nlink_t;
__extension__ typedef long int __off_t;
__extension__ typedef int __ssize_t;
__extension__ typedef long int __blksize_t;
__extension__ typedef long int __blkcnt_t;
__extension__ typedef long int __time_t;
__extension__ typedef int __pid_t;

typedef __ssize_t ssize_t;
typedef __pid_t pid_t;

extern char **environ;

struct stat {
  __dev_t st_dev;
  unsigned short int __pad1;
  __ino_t st_ino;
  __mode_t st_mode;
  __nlink_t st_nlink;
  __uid_t st_uid;
  __gid_t st_gid;
  __dev_t st_rdev;
  unsigned short int __pad2;
  __off_t st_size;
  __blksize_t st_blksize;
  __blkcnt_t st_blocks;
  __time_t st_atime;
  unsigned long int st_atimensec;
  __time_t st_mtime;
  unsigned long int st_mtimensec;
  __time_t st_ctime;
  unsigned long int st_ctimensec;
  unsigned long int __unused4;
  unsigned long int __unused5;
};

struct utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

extern size_t strlen(__const char *__s) __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1)));
extern void *malloc(size_t __size) __attribute__((__nothrow__)) __attribute__((__malloc__)) ;
extern void *realloc(void *__ptr, size_t __size) __attribute__((__nothrow__)) __attribute__((__malloc__)) __attribute__((__warn_unused_result__));
extern void free(void *__ptr) __attribute__((__nothrow__));
extern void *memcpy(void *__restrict __dest,   __const void *__restrict __src, size_t __n) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strcpy(char *__restrict __dest, __const char *__restrict __src) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern ssize_t readlink(__const char *__restrict __path, char *__restrict __buf, size_t __len) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2))) ;
extern int lstat(__const char *__restrict __file, struct stat *__restrict __buf) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strcat(char *__restrict __dest, __const char *__restrict __src) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *strdup(__const char *__s) __attribute__((__nothrow__)) __attribute__((__malloc__)) __attribute__((__nonnull__(1)));
extern int strcmp(__const char *__s1, __const char *__s2) __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int strncmp(__const char *__s1, __const char *__s2, size_t __n) __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern ssize_t write(int __fd, __const void *__buf, size_t __n) ;
extern void exit(int __status) __attribute__((__nothrow__)) __attribute__((__noreturn__));
extern char *strstr(__const char *__haystack, __const char *__needle) __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern int stat(__const char *__restrict __file, struct stat *__restrict __buf) __attribute__((__nothrow__)) __attribute__((__nonnull__(1, 2)));
extern char *getenv(__const char *__name) __attribute__((__nothrow__)) __attribute__((__nonnull__(1))) ;
extern int execv(__const char *__path, char *__const __argv[]) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int execve(__const char *__path, char *__const __argv[], char *__const __envp[]) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));
extern int uname(struct utsname *__name) __attribute__((__nothrow__));
extern int open(__const char *__file, int __oflag, ...) __attribute__((__nonnull__(1)));
extern ssize_t read(int __fd, void *__buf, size_t __nbytes) ;
extern int close(int __fd);
extern int memcmp(__const void *__s1, __const void *__s2, size_t __n) __attribute__((__nothrow__)) __attribute__((__pure__)) __attribute__((__nonnull__(1, 2)));
extern __pid_t fork(void) __attribute__((__nothrow__));
extern __pid_t waitpid(__pid_t __pid, int *__stat_loc, int __options);
extern int unlink(__const char *__name) __attribute__((__nothrow__)) __attribute__((__nonnull__(1)));

#endif  /* _MINIINCH_ */
