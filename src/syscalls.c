/* newlib 系统调用桩：本工程不用文件 IO，只为消掉链接警告 */
#include <errno.h>
#include <sys/stat.h>

int _close(int fd)                         { (void)fd; return -1; }
int _lseek(int fd, int ptr, int dir)       { (void)fd; (void)ptr; (void)dir; return 0; }
int _read(int fd, char *p, int len)        { (void)fd; (void)p; (void)len; return 0; }
int _write(int fd, char *p, int len)       { (void)fd; (void)p; return len; }
int _fstat(int fd, struct stat *st)        { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                        { (void)fd; return 1; }
