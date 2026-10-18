/**
 * Injectable I/O implementation
 */

#define _POSIX_C_SOURCE 200809L

#include "io/io.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// Global I/O operations pointer
const io_ops_t *io = nullptr;

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

static ssize_t
real_read(int fd, void *buf, size_t count)
{
    return read(fd, buf, count);
}

static ssize_t
real_write(int fd, const void *buf, size_t count)
{
    return write(fd, buf, count);
}

static ssize_t
real_pread(int fd, void *buf, size_t count, off_t offset)
{
    return pread(fd, buf, count, offset);
}

static ssize_t
real_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    return pwrite(fd, buf, count, offset);
}

static int
real_fsync(int fd)
{
    return fsync(fd);
}

static int
real_fdatasync(int fd)
{
#ifdef __APPLE__
    // macOS doesn't have fdatasync, use fsync
    return fsync(fd);
#else
    return fdatasync(fd);
#endif
}

static int
real_open(const char *pathname, int flags, mode_t mode)
{
    return open(pathname, flags, mode);
}

static int
real_close(int fd)
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

static bool
should_inject_fault(io_fault_type_t expected_type)
{
    if (fault_state.type == IO_FAULT_NONE) {
        return false;
    }

    // Only count calls for the fault type we're injecting
    if (fault_state.type != expected_type) {
        return false;
    }

    fault_state.call_count++;

    if (fault_state.nth_call > 0 && fault_state.call_count == fault_state.nth_call) {
        return true;
    }

    return false;
}

static ssize_t
fault_write(int fd, const void *buf, size_t count)
{
    if (should_inject_fault(IO_FAULT_TORN_WRITE) || should_inject_fault(IO_FAULT_WRITE_ERROR)) {
        switch (fault_state.type) {
        case IO_FAULT_TORN_WRITE:
            // Write only half the data (simulate power loss)
            return write(fd, buf, count / 2);

        case IO_FAULT_WRITE_ERROR:
            errno = EIO;
            return -1;

        default:
            break;
        }
    }

    return write(fd, buf, count);
}

static ssize_t
fault_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    if (should_inject_fault(IO_FAULT_TORN_WRITE) || should_inject_fault(IO_FAULT_WRITE_ERROR)) {
        switch (fault_state.type) {
        case IO_FAULT_TORN_WRITE:
            return pwrite(fd, buf, count / 2, offset);

        case IO_FAULT_WRITE_ERROR:
            errno = EIO;
            return -1;

        default:
            break;
        }
    }

    return pwrite(fd, buf, count, offset);
}

static int
fault_fsync(int fd)
{
    if (should_inject_fault(IO_FAULT_FSYNC_FAIL) || should_inject_fault(IO_FAULT_FSYNC_DROP)) {
        switch (fault_state.type) {
        case IO_FAULT_FSYNC_FAIL:
            errno = EIO;
            return -1;

        case IO_FAULT_FSYNC_DROP:
            // Succeed but don't actually sync (simulate fsyncgate)
            return 0;

        default:
            break;
        }
    }

    return fsync(fd);
}

static ssize_t
fault_read(int fd, void *buf, size_t count)
{
    if (should_inject_fault(IO_FAULT_READ_ERROR)) {
        errno = EIO;
        return -1;
    }

    return read(fd, buf, count);
}

static ssize_t
fault_pread(int fd, void *buf, size_t count, off_t offset)
{
    if (should_inject_fault(IO_FAULT_READ_ERROR)) {
        errno = EIO;
        return -1;
    }

    return pread(fd, buf, count, offset);
}

const io_ops_t io_fault_inject_ops = {
    .read = fault_read,
    .write = fault_write,
    .pread = fault_pread,
    .pwrite = fault_pwrite,
    .fsync = fault_fsync,
    .fdatasync = real_fdatasync,  // No fault injection for fdatasync yet
    .open = real_open,
    .close = real_close,
};

/*
 * Public API
 */

void
io_inject_fault(io_fault_type_t type, int nth)
{
    fault_state.type = type;
    fault_state.nth_call = nth;
    fault_state.call_count = 0;
}

void
io_reset_faults(void)
{
    fault_state.type = IO_FAULT_NONE;
    fault_state.nth_call = 0;
    fault_state.call_count = 0;
}

void
io_init(bool use_real)
{
    io = use_real ? &io_real_ops : &io_fault_inject_ops;
    io_reset_faults();
}
