/**
 * The real I/O operations
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE // flock

#include "io/io.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

// Global I/O operations pointer, real syscalls until a test replaces it
const io_ops_t *io = &io_real_ops;

/*
 * Real I/O operations (direct syscalls)
 */

static ssize_t real_read(int fd, void *buf, size_t count)
{
    return read(fd, buf, count);
}

static ssize_t real_write(int fd, const void *buf, size_t count)
{
    return write(fd, buf, count);
}

static ssize_t real_pread(int fd, void *buf, size_t count, off_t offset)
{
    return pread(fd, buf, count, offset);
}

static ssize_t real_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    return pwrite(fd, buf, count, offset);
}

static int real_fsync(int fd)
{
    return fsync(fd);
}

static int real_fdatasync(int fd)
{
    return fdatasync(fd);
}

static int real_open(const char *pathname, int flags, mode_t mode)
{
    return open(pathname, flags, mode);
}

static int real_close(int fd)
{
    return close(fd);
}

static int real_flock(int fd, int operation)
{
    return flock(fd, operation);
}

static int real_fstat(int fd, struct stat *status)
{
    return fstat(fd, status);
}

const io_ops_t io_real_ops = {
    .read = real_read,
    .write = real_write,
    .pread = real_pread,
    .pwrite = real_pwrite,
    .fsync = real_fsync,
    .fdatasync = real_fdatasync,
    .open = real_open,
    .close = real_close,
    .flock = real_flock,
    .fstat = real_fstat,
};
