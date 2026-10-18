/**
 * Tests for injectable I/O interface
 */

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "io/io.h"

static int setup([[maybe_unused]] void **state)
{
    io_init(true);
    return 0;
}

static int teardown([[maybe_unused]] void **state)
{
    io_reset_faults();
    return 0;
}

static int open_scratch_file(const char *path)
{
    int fd = io->open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fail();
    }
    return fd;
}

static void close_scratch_file(int fd, const char *path)
{
    io->close(fd);
    unlink(path);
}

static void test_real_io_operations([[maybe_unused]] void **state)
{
    const char *path = "/tmp/tieldb_test_io.dat";
    const char *data = "Hello, TielDB!";
    char buf[128] = {};
    int fd = open_scratch_file(path);

    assert_int_equal(io->write(fd, data, strlen(data)), (ssize_t) strlen(data));
    assert_int_equal(io->fsync(fd), 0);
    assert_true(io->pread(fd, buf, sizeof(buf), 0) > 0);
    assert_string_equal(buf, data);
    close_scratch_file(fd, path);
}

static void test_fault_injection_torn_write([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_torn.dat";
    const char *data = "0123456789ABCDEF";
    char buf[128] = {};
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_TORN_WRITE, 1);
    assert_int_equal(io->write(fd, data, strlen(data)), (ssize_t) strlen(data) / 2);
    assert_int_equal(io_real_ops.pread(fd, buf, sizeof(buf), 0), (ssize_t) strlen(data) / 2);
    close_scratch_file(fd, path);
}

static void test_fault_injection_fsync_fail([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_fsync.dat";
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);
    assert_int_equal(io->write(fd, "test", 4), 4);
    assert_int_equal(io->fsync(fd), -1);
    close_scratch_file(fd, path);
}

static void test_fault_on_nth_call([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_nth.dat";
    const ssize_t expected[] = {1, 1, -1, 1};
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_WRITE_ERROR, 3);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        assert_int_equal(io->write(fd, "X", 1), expected[i]);
    }
    close_scratch_file(fd, path);
}

static void test_reset_faults([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_reset.dat";
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    io_reset_faults();
    assert_int_equal(io->write(fd, "X", 1), 1);
    close_scratch_file(fd, path);
}

static void test_fault_with_nth_zero_is_disabled([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_nth_zero.dat";
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_TORN_WRITE, 0);
    assert_int_equal(io->write(fd, "abcd", 4), 4);
    close_scratch_file(fd, path);
}

static void test_real_positioned_and_plain_io([[maybe_unused]] void **state)
{
    const char *path = "/tmp/tieldb_test_positioned.dat";
    char buf[4] = {};
    int fd = open_scratch_file(path);

    assert_int_equal(io->pwrite(fd, "abcd", 4, 0), 4);
    assert_int_equal(io->fdatasync(fd), 0);
    assert_int_equal(lseek(fd, 0, SEEK_SET), 0);
    assert_int_equal(io->read(fd, buf, sizeof(buf)), 4);

    assert_memory_equal(buf, "abcd", 4);
    close_scratch_file(fd, path);
}

static void test_fault_read_error_is_one_shot([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_read_error.dat";
    char buf[1];
    int fd = open_scratch_file(path);
    assert_int_equal(io->write(fd, "x", 1), 1);
    assert_int_equal(lseek(fd, 0, SEEK_SET), 0);

    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    errno = 0;
    assert_int_equal(io->read(fd, buf, 1), -1);
    assert_int_equal(errno, EIO);
    assert_int_equal(io->read(fd, buf, 1), 1);
    close_scratch_file(fd, path);
}

static void test_fault_pread_error_is_one_shot([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_pread_error.dat";
    char buf[1];
    int fd = open_scratch_file(path);
    assert_int_equal(io->write(fd, "x", 1), 1);

    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    errno = 0;
    assert_int_equal(io->pread(fd, buf, 1, 0), -1);
    assert_int_equal(errno, EIO);
    assert_int_equal(io->pread(fd, buf, 1, 0), 1);
    close_scratch_file(fd, path);
}

static void test_fault_pwrite_error_and_torn([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_pwrite_faults.dat";
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    errno = 0;
    assert_int_equal(io->pwrite(fd, "abcd", 4, 0), -1);
    assert_int_equal(errno, EIO);

    io_inject_fault(IO_FAULT_TORN_WRITE, 1);
    assert_int_equal(io->pwrite(fd, "abcd", 4, 0), 2);
    assert_int_equal(io->pwrite(fd, "abcd", 4, 0), 4);
    close_scratch_file(fd, path);
}

static void test_fault_write_error_sets_eio([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_write_error.dat";
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    errno = 0;
    assert_int_equal(io->write(fd, "x", 1), -1);
    assert_int_equal(errno, EIO);
    close_scratch_file(fd, path);
}

static void test_fault_fsync_drop_reports_success([[maybe_unused]] void **state)
{
    io_init(false);
    const char *path = "/tmp/tieldb_test_fsync_drop.dat";
    int fd = open_scratch_file(path);

    io_inject_fault(IO_FAULT_FSYNC_DROP, 1);
    assert_int_equal(io->fsync(fd), 0);
    assert_int_equal(io->fsync(fd), 0);
    close_scratch_file(fd, path);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_real_io_operations, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_injection_torn_write, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_injection_fsync_fail, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_on_nth_call, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reset_faults, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_with_nth_zero_is_disabled, setup, teardown),
        cmocka_unit_test_setup_teardown(test_real_positioned_and_plain_io, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_read_error_is_one_shot, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_pread_error_is_one_shot, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_pwrite_error_and_torn, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_write_error_sets_eio, setup, teardown),
        cmocka_unit_test_setup_teardown(test_fault_fsync_drop_reports_success, setup, teardown),
    };

    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
