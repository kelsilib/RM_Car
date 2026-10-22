/* C库需要这些文件读写函数才能完成链接，但这个单片机程序没有操作系统文件。
 * 所以下面统一返回失败。想打印到CH340要用board_debug，直接printf不会输出到串口。
 * EBADF是“无效文件句柄”的错误码，平时测试无需修改这里。 */
#include <errno.h>
int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _read(int fd, char *buf, int len)
{ (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _write(int fd, char *buf, int len)
{ (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _lseek(int fd, int offset, int whence)
{ (void)fd; (void)offset; (void)whence; errno = EBADF; return -1; }
