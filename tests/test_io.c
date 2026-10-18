/**
 * Tests for injectable I/O interface
 */

#include <cmocka.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/io.h"

// Test fixture setup/teardown
static int setup(void **state)
{
    (void) state;  // Unused
    io_init(true); // Start with real I/O
    return 0;
}

static int teardown(void **state)
{
    (void) state; // Unused
    io_reset_faults();
    return 0;
}

// Test: Real I/O operations work
static void test_real_io_operations(void **state)
{
    (void) state;

    const char *test_file = "/tmp/tieldb_test_io.dat";
    const char *test_data = "Hello, TielDB!";
    char read_buf[128] = {};

    // Write
    int fd = io->open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert_true(fd >= 0);

    ssize_t written = io->write(fd, test_data, strlen(test_data));
    assert_int_equal(written, (ssize_t) strlen(test_data));

    int sync_result = io->fsync(fd);
    assert_int_equal(sync_result, 0);

    // Read back
    ssize_t read_bytes = io->pread(fd, read_buf, sizeof(read_buf), 0);
    assert_true(read_bytes > 0);
    assert_string_equal(read_buf, test_data);

    io->close(fd);
    unlink(test_file);
}

// Test: Fault injection for torn write
static void test_fault_injection_torn_write(void **state)
{
    (void) state;

    io_init(false); // Use fault injection

    const char *test_file = "/tmp/tieldb_test_torn.dat";
    const char *test_data = "0123456789ABCDEF"; // 16 bytes
    char read_buf[128] = {};

    // Inject torn write on 1st write call
    io_inject_fault(IO_FAULT_TORN_WRITE, 1);

    int fd = io->open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert_true(fd >= 0);

    // This write will be torn (only half written)
    ssize_t written = io->write(fd, test_data, strlen(test_data));
    assert_int_equal(written, (ssize_t) (strlen(test_data) / 2)); // Only half

    io->close(fd);

    // Verify only half was written
    fd = open(test_file, O_RDONLY);
    ssize_t read_bytes = read(fd, read_buf, sizeof(read_buf));
    assert_int_equal(read_bytes, 8); // Half of 16

    close(fd);
    unlink(test_file);
}

// Test: Fault injection for fsync failure
static void test_fault_injection_fsync_fail(void **state)
{
    (void) state;

    io_init(false); // Use fault injection

    const char *test_file = "/tmp/tieldb_test_fsync.dat";

    // Inject fsync failure on 1st fsync call
    io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);

    int fd = io->open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert_true(fd >= 0);

    const char *data = "test";
    io->write(fd, data, strlen(data));

    // This fsync will fail
    int result = io->fsync(fd);
    assert_int_equal(result, -1);

    io->close(fd);
    unlink(test_file);
}

// Test: Multiple calls, fault on Nth
static void test_fault_on_nth_call(void **state)
{
    (void) state;

    io_init(false);

    const char *test_file = "/tmp/tieldb_test_nth.dat";

    // Inject write error on 3rd write call
    io_inject_fault(IO_FAULT_WRITE_ERROR, 3);

    int fd = io->open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert_true(fd >= 0);

    char data = 'X';

    // First write: succeeds
    assert_int_equal(io->write(fd, &data, 1), 1);

    // Second write: succeeds
    assert_int_equal(io->write(fd, &data, 1), 1);

    // Third write: fails (fault injected)
    assert_int_equal(io->write(fd, &data, 1), -1);

    // Fourth write: succeeds (fault was one-time)
    assert_int_equal(io->write(fd, &data, 1), 1);

    io->close(fd);
    unlink(test_file);
}

// Test: Reset faults
static void test_reset_faults(void **state)
{
    (void) state;

    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);

    // Reset faults
    io_reset_faults();

    const char *test_file = "/tmp/tieldb_test_reset.dat";
    int fd = io->open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);

    char data = 'X';

    // Should succeed (fault was reset)
    assert_int_equal(io->write(fd, &data, 1), 1);

    io->close(fd);
    unlink(test_file);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_real_io_operations, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_injection_torn_write, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_injection_fsync_fail, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_on_nth_call, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reset_faults, setup, teardown),
    };

    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
