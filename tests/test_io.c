/**
 * Tests for the real I/O operations (the fault injection is in tests/io_fault.h and is exercised by
 * the tests of the page file, the one-page file and the data directory)
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
#include <sys/file.h>
#include <unistd.h>

#include <cmocka.h>

#include "io/io.h"

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

static void test_io_defaults_to_real_operations([[maybe_unused]] void **state)
{
    assert_ptr_equal(io, &io_real_ops);
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

static void test_real_fstat_reports_the_size([[maybe_unused]] void **state)
{
    const char *path = "/tmp/tieldb_test_fstat.dat";
    int fd = open_scratch_file(path);
    struct stat status;

    assert_int_equal(io->write(fd, "abcd", 4), 4);
    assert_int_equal(io->fstat(fd, &status), 0);
    assert_int_equal(status.st_size, 4);
    close_scratch_file(fd, path);
}

static void test_real_flock_conflicts_between_descriptors([[maybe_unused]] void **state)
{
    const char *path = "/tmp/tieldb_test_flock.dat";
    int first = open_scratch_file(path);
    int second = io->open(path, O_RDWR, 0);

    assert_true(second >= 0);
    assert_int_equal(io->flock(first, LOCK_EX | LOCK_NB), 0);
    errno = 0;
    assert_int_equal(io->flock(second, LOCK_EX | LOCK_NB), -1);
    assert_int_equal(errno, EWOULDBLOCK);
    assert_int_equal(io->flock(first, LOCK_UN), 0);
    assert_int_equal(io->flock(second, LOCK_EX | LOCK_NB), 0);
    io->close(second);
    close_scratch_file(first, path);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_io_defaults_to_real_operations),
    cmocka_unit_test(test_real_io_operations),
    cmocka_unit_test(test_real_positioned_and_plain_io),
    cmocka_unit_test(test_real_fstat_reports_the_size),
    cmocka_unit_test(test_real_flock_conflicts_between_descriptors),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
