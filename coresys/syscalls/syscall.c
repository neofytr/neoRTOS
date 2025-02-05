#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

extern uint8_t _heap_start;
extern uint8_t _heap_end;

static uint8_t *curr_heap_end = (uint8_t *)&_heap_start;

void *_sbrk(intptr_t incr)
{
    uint8_t *heap_end = (uint8_t *)&_heap_end;
    uint8_t *prev_heap_end;

    if ((curr_heap_end + incr) > heap_end)
    {
        return (void *)-1;
    }

    prev_heap_end = curr_heap_end;

    curr_heap_end += incr;

    return prev_heap_end;
}

void _exit(int status)
{
    (void)status; // Ignore the argument
    while (1)
    { // Infinite loop to "hang" the program
    }
}

ssize_t _read(int fd, void *buf, size_t count)
{
    (void)fd;    // Ignore file descriptor
    (void)buf;   // Ignore buffer
    (void)count; // Ignore count
    return 0;    // Return 0 indicating no data read
}

ssize_t _write(int fd, const void *buf, size_t count)
{
    (void)fd;     // Ignore file descriptor
    (void)buf;    // Ignore buffer
    (void)count;  // Ignore count
    return count; // Return count indicating that "all" data was written (no-op)
}

int _close(int fd)
{
    (void)fd; // Ignore file descriptor
    return 0; // Return 0 to indicate success
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd;     // Ignore file descriptor
    (void)offset; // Ignore offset
    (void)whence; // Ignore whence
    return 0;     // Return 0 to indicate success
}

int _fstat(int fd, struct stat *buf)
{
    (void)fd;  // Ignore file descriptor
    (void)buf; // Ignore buffer
    return 0;  // Return 0 to indicate success
}

int _isatty(int fd)
{
    (void)fd; // Ignore file descriptor
    return 0; // Return 0 indicating not a terminal
}

int _kill(pid_t pid, int sig)
{
    (void)pid; // Ignore process ID
    (void)sig; // Ignore signal
    return -1; // Return -1 to indicate error
}

int _fcntl(int fd, int cmd, ...)
{
    (void)fd;  // Ignore file descriptor
    (void)cmd; // Ignore command
    return -1; // Return -1 to indicate error
}

int _dup(int oldfd)
{
    (void)oldfd; // Ignore old file descriptor
    return -1;   // Return -1 to indicate error
}

int _dup2(int oldfd, int newfd)
{
    (void)oldfd; // Ignore old file descriptor
    (void)newfd; // Ignore new file descriptor
    return -1;   // Return -1 to indicate error
}
