/**
 * Injectable I/O interface for TielDB
 *
 * All file I/O goes through this interface, so that tests can replace it with operations that
 * simulate torn writes, dropped fsyncs and other failures (see tests/io_fault.h). The server
 * contains only the real operations.
 */

#pragma once

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * I/O operations vtable
 */
typedef struct io_ops {
    /**
     * Read from file descriptor
     */
    ssize_t (*read)(int fd, void *buf, size_t count);

    /**
     * Write to file descriptor
     */
    ssize_t (*write)(int fd, const void *buf, size_t count);

    /**
     * Positioned read
     */
    ssize_t (*pread)(int fd, void *buf, size_t count, off_t offset);

    /**
     * Positioned write
     */
    ssize_t (*pwrite)(int fd, const void *buf, size_t count, off_t offset);

    /**
     * Sync file data to disk
     */
    int (*fsync)(int fd);

    /**
     * Sync file data (not metadata) to disk
     */
    int (*fdatasync)(int fd);

    /**
     * Open file
     */
    int (*open)(const char *pathname, int flags, mode_t mode);

    /**
     * Close file
     */
    int (*close)(int fd);

    /**
     * Apply or remove an advisory lock on a file (see flock(2)); operation is LOCK_EX, LOCK_SH or
     * LOCK_UN, possibly with LOCK_NB. A lock belongs to the open file description, so it is
     * released when the descriptor is closed or the process ends, and two descriptors of the same
     * file conflict, even in one process.
     */
    int (*flock)(int fd, int operation);

    /**
     * Status of an open file (see fstat(2)); its size is status->st_size
     */
    int (*fstat)(int fd, struct stat *status);
} io_ops_t;

/**
 * Global I/O operations pointer
 * Real syscalls by default; tests point it at the fault injection operations (tests/io_fault.h)
 */
extern const io_ops_t *io;

/**
 * Real I/O operations (direct syscalls)
 */
extern const io_ops_t io_real_ops;
