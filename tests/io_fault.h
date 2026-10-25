/**
 * Fault injection for the I/O interface, for tests only: io_init(false) makes io point at
 * operations that fail or misbehave when io_inject_fault says so, and io_init(true) points it at
 * the real operations again. The server never contains this code.
 */

#pragma once

#include "io/io.h"

typedef enum {
    IO_FAULT_NONE,
    IO_FAULT_TORN_WRITE,     // Partial write (simulate power loss mid-write)
    IO_FAULT_FSYNC_FAIL,     // fsync returns error
    IO_FAULT_FSYNC_DROP,     // fsync succeeds but doesn't persist (fsyncgate)
    IO_FAULT_READ_ERROR,     // read fails
    IO_FAULT_WRITE_ERROR,    // write fails
    IO_FAULT_STAT_ERROR,     // fstat fails
    IO_FAULT_DIRECT_REFUSED, // open with O_DIRECT fails with EINVAL, as on a file system without it
} io_fault_type_t;

/**
 * Configure fault injection
 *
 * @param type   Type of fault to inject
 * @param nth    Inject fault on the Nth call (0 = disable)
 */
void io_inject_fault(io_fault_type_t type, int nth);

/** Reset fault injection state */
void io_reset_faults(void);

/**
 * Choose the I/O operations and reset the faults
 *
 * @param use_real  If true, use real I/O; if false, use fault injection
 */
void io_init(bool use_real);
