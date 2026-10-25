/**
 * Fault injection for the I/O interface, for tests only
 */

#include "io_fault.h"

#include <errno.h>

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
        return io_real_ops.write(fd, buf, count / 2);
    }

    return io_real_ops.write(fd, buf, count);
}

static ssize_t fault_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    if (should_inject_fault(IO_FAULT_WRITE_ERROR)) {
        errno = EIO;
        return -1;
    }
    if (should_inject_fault(IO_FAULT_TORN_WRITE)) {
        return io_real_ops.pwrite(fd, buf, count / 2, offset);
    }

    return io_real_ops.pwrite(fd, buf, count, offset);
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

    return io_real_ops.fsync(fd);
}

static ssize_t fault_read(int fd, void *buf, size_t count)
{
    if (should_inject_fault(IO_FAULT_READ_ERROR)) {
        errno = EIO;
        return -1;
    }

    return io_real_ops.read(fd, buf, count);
}

static ssize_t fault_pread(int fd, void *buf, size_t count, off_t offset)
{
    if (should_inject_fault(IO_FAULT_READ_ERROR)) {
        errno = EIO;
        return -1;
    }

    return io_real_ops.pread(fd, buf, count, offset);
}

static int fault_fstat(int fd, struct stat *status)
{
    if (should_inject_fault(IO_FAULT_STAT_ERROR)) {
        errno = EIO;
        return -1;
    }

    return io_real_ops.fstat(fd, status);
}

static io_ops_t io_fault_inject_ops;

static void build_fault_inject_ops(void)
{
    io_fault_inject_ops = io_real_ops;
    io_fault_inject_ops.read = fault_read;
    io_fault_inject_ops.write = fault_write;
    io_fault_inject_ops.pread = fault_pread;
    io_fault_inject_ops.pwrite = fault_pwrite;
    io_fault_inject_ops.fsync = fault_fsync;
    io_fault_inject_ops.fstat = fault_fstat;
}

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
    build_fault_inject_ops();
    io = use_real ? &io_real_ops : &io_fault_inject_ops;
    io_reset_faults();
}
