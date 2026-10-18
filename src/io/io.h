/**
 * Injectable I/O interface for TielDB
 *
 * All file I/O goes through this interface to enable fault injection
 * during testing. This allows simulation of torn writes, dropped fsyncs,
 * and other failure scenarios critical for testing durability.
 */

#pragma once

#include <stddef.h>
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
} io_ops_t;

/**
 * Global I/O operations pointer
 * Set to io_real for production, io_fault_inject for testing
 */
extern const io_ops_t *io;

/**
 * Real I/O operations (direct syscalls)
 */
extern const io_ops_t io_real_ops;

/**
 * Fault injection operations (for testing)
 */
extern const io_ops_t io_fault_inject_ops;

/**
 * Fault injection types
 */
typedef enum {
    IO_FAULT_NONE,
    IO_FAULT_TORN_WRITE,  // Partial write (simulate power loss mid-write)
    IO_FAULT_FSYNC_FAIL,  // fsync returns error
    IO_FAULT_FSYNC_DROP,  // fsync succeeds but doesn't persist (fsyncgate)
    IO_FAULT_READ_ERROR,  // read fails
    IO_FAULT_WRITE_ERROR, // write fails
} io_fault_type_t;

/**
 * Configure fault injection
 *
 * @param type   Type of fault to inject
 * @param nth    Inject fault on the Nth call (0 = disable)
 */
void io_inject_fault(io_fault_type_t type, int nth);

/**
 * Reset fault injection state
 */
void io_reset_faults(void);

/**
 * Initialize I/O subsystem
 *
 * @param use_real  If true, use real I/O; if false, use fault injection
 */
void io_init(bool use_real);
