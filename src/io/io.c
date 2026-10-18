/**
 * Injectable I/O implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "io/io.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// Global I/O operations pointer, real syscalls until io_init selects otherwise
const io_ops_t *io = &io_real_ops;

// Fault injection state
static struct {
    io_fault_type_t type;
    int nth_call;
    int call_count;
} fault_state = {
    .type = IO_FAULT_NONE,
    .nth_call = 0,
    .call_count = 0,
};

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

const io_ops_t io_real_ops = {
    .read = real_read,
    .write = real_write,
    .pread = real_pread,
    .pwrite = real_pwrite,
    .fsync = real_fsync,
    .fdatasync = real_fdatasync,
    .open = real_open,
    .close = real_close,
};

/*
 * Fault injection operations
 */

static bool should_inject_fault(io_fault_type_t expected_type)
{
    if (fault_state.type == IO_FAULT_NONE) {
        return false;
    }

    // Only count calls for the fault type we're injecting
    if (fault_state.type != expected_type) {
        return false;
    }

    fault_state.call_count++;

    return fault_state.nth_call > 0 && fault_state.call_count == fault_state.nth_call;
}

static ssize_t fault_write(int fd, const void *buf, size_t count)
{
    if (should_inject_fault(IO_FAULT_WRITE_ERROR)) {
        errno = EIO;
        return -1;
    }
    if (should_inject_fault(IO_FAULT_TORN_WRITE)) {
        // Write only half the data (simulate power loss)
        return real_write(fd, buf, count / 2);
    }

    return real_write(fd, buf, count);
}

static ssize_t fault_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    if (should_inject_fault(IO_FAULT_WRITE_ERROR)) {
        errno = EIO;
        return -1;
    }
    if (should_inject_fault(IO_FAULT_TORN_WRITE)) {
        return real_pwrite(fd, buf, count / 2, offset);
    }

    return real_pwrite(fd, buf, count, offset);
}

static int fault_fsync(int fd)
{
    if (should_inject_fault(IO_FAULT_FSYNC_FAIL)) {
        errno = EIO;
        return -1;
    }
    if (should_inject_fault(IO_FAULT_FSYNC_DROP)) {
        // Succeed but don't actually sync (simulate fsyncgate)
        return 0;
    }

    return real_fsync(fd);
}

static ssize_t fault_read(int fd, void *buf, size_t count)
{
    if (should_inject_fault(IO_FAULT_READ_ERROR)) {
        errno = EIO;
        return -1;
    }

    return real_read(fd, buf, count);
}

static ssize_t fault_pread(int fd, void *buf, size_t count, off_t offset)
{
    if (should_inject_fault(IO_FAULT_READ_ERROR)) {
        errno = EIO;
        return -1;
    }

    return real_pread(fd, buf, count, offset);
}

const io_ops_t io_fault_inject_ops = {
    .read = fault_read,
    .write = fault_write,
    .pread = fault_pread,
    .pwrite = fault_pwrite,
    .fsync = fault_fsync,
    .fdatasync = real_fdatasync, // No fault injection for fdatasync yet
    .open = real_open,
    .close = real_close,
};

/*
 * Public API
 */

void io_inject_fault(io_fault_type_t type, int nth)
{
    fault_state.type = type;
    fault_state.nth_call = nth;
    fault_state.call_count = 0;
}

void io_reset_faults(void)
{
    fault_state.type = IO_FAULT_NONE;
    fault_state.nth_call = 0;
    fault_state.call_count = 0;
}

void io_init(bool use_real)
{
    io = use_real ? &io_real_ops : &io_fault_inject_ops;
    io_reset_faults();
}
