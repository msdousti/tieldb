/**
 * Tests for files of pages
 *
 * Each test works on a scratch file that the setup creates and the teardown removes.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "io/io.h"
#include "storage/page_file.h"

static constexpr size_t SLOT_COUNT_OFFSET = 16;

static const char PATH_TEMPLATE[] = "/tmp/tieldb_page_file_XXXXXX";
static char path[sizeof(PATH_TEMPLATE)];
static page_file_t *file;

static int setup([[maybe_unused]] void **state)
{
    memcpy(path, PATH_TEMPLATE, sizeof(path));
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    io_init(true);
    file = page_file_open(path);
    return file == nullptr ? -1 : 0;
}

static int teardown([[maybe_unused]] void **state)
{
    io_init(true);
    int status = page_file_close(file);
    unlink(path);
    return status;
}

static page_t page_holding(const char *text)
{
    page_t page;
    page_init(&page, PAGE_TYPE_HEAP);
    assert_int_equal(page_insert(&page, text, strlen(text)), 0);
    return page;
}

static void assert_read_fails(size_t page_number, int error)
{
    page_t page;
    errno = 0;
    assert_int_equal(page_file_read(file, page_number, &page), -1);
    assert_int_equal(errno, error);
}

static void assert_page_holds(size_t page_number, const char *text)
{
    page_t page;
    size_t length = 0;
    assert_int_equal(page_file_read(file, page_number, &page), 0);
    const void *tuple = page_get(&page, 0, &length);
    if (tuple == nullptr) {
        fail();
    }
    assert_int_equal(length, strlen(text));
    assert_memory_equal(tuple, text, length);
}

static void test_a_written_page_reads_back_with_its_checksum([[maybe_unused]] void **state)
{
    page_t written = page_holding("hello");
    page_t expected = written;
    page_t read;

    page_set_checksum(&expected);
    assert_int_equal(page_file_write(file, 0, &written), 0);
    assert_int_equal(page_file_read(file, 0, &read), 0);
    assert_memory_equal(read.bytes, expected.bytes, PAGE_SIZE);
    assert_int_equal(page_verify_checksum(&written), -1);
}

static void test_pages_are_kept_apart([[maybe_unused]] void **state)
{
    page_t first = page_holding("first");
    page_t second = page_holding("second");

    assert_int_equal(page_file_write(file, 0, &first), 0);
    assert_int_equal(page_file_write(file, 1, &second), 0);
    assert_page_holds(0, "first");
    assert_page_holds(1, "second");
    assert_read_fails(2, ENOENT);
}

static void reopen_file(void)
{
    assert_int_equal(page_file_sync(file), 0);
    assert_int_equal(page_file_close(file), 0);
    file = page_file_open(path);
    if (file == nullptr) {
        fail();
    }
}

static void test_pages_survive_closing_and_reopening([[maybe_unused]] void **state)
{
    page_t page = page_holding("kept");

    assert_int_equal(page_file_write(file, 0, &page), 0);
    reopen_file();
    assert_page_holds(0, "kept");
}

static void test_a_page_that_was_never_written_is_corrupt([[maybe_unused]] void **state)
{
    page_t last = page_holding("last");

    assert_int_equal(page_file_write(file, 3, &last), 0);
    assert_read_fails(1, EBADMSG);
    assert_page_holds(3, "last");
}

static void test_a_truncated_page_is_corrupt([[maybe_unused]] void **state)
{
    page_t page = page_holding("cut");

    assert_int_equal(page_file_write(file, 0, &page), 0);
    assert_int_equal(truncate(path, PAGE_SIZE / 2), 0);
    assert_read_fails(0, EBADMSG);
}

static void test_a_changed_byte_on_disk_is_caught([[maybe_unused]] void **state)
{
    page_t page = page_holding("flip");
    int fd = open(path, O_RDWR);
    unsigned char byte = 0;

    assert_int_equal(page_file_write(file, 0, &page), 0);
    assert_int_equal(pread(fd, &byte, 1, PAGE_SIZE - 1), 1);
    byte ^= 0x01;
    assert_int_equal(pwrite(fd, &byte, 1, PAGE_SIZE - 1), 1);
    close(fd);
    assert_read_fails(0, EBADMSG);
}

static void
test_a_page_with_a_good_checksum_but_a_bad_structure_is_corrupt([[maybe_unused]] void **state)
{
    page_t page = page_holding("bad");
    const uint16_t too_many = UINT16_MAX;
    int fd = open(path, O_RDWR);

    memcpy(page.bytes + SLOT_COUNT_OFFSET, &too_many, sizeof(too_many));
    page_set_checksum(&page);
    assert_int_equal(pwrite(fd, page.bytes, PAGE_SIZE, 0), PAGE_SIZE);
    close(fd);
    assert_read_fails(0, EBADMSG);
}

static void test_open_does_not_create_a_missing_file([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(page_file_open("/tmp/tieldb_no_such_directory/pages"));
    assert_int_equal(errno, ENOENT);
    assert_int_equal(access("/tmp/tieldb_no_such_directory", F_OK), -1);
}

static void test_create_makes_a_new_file([[maybe_unused]] void **state)
{
    page_t page = page_holding("new");

    assert_int_equal(unlink(path), 0);
    page_file_t *created = page_file_create(path);
    if (created == nullptr) {
        fail();
    }
    assert_int_equal(page_file_write(created, 0, &page), 0);
    assert_int_equal(page_file_close(created), 0);
}

static void test_create_refuses_an_existing_file([[maybe_unused]] void **state)
{
    page_t page = page_holding("kept");

    assert_int_equal(page_file_write(file, 0, &page), 0);
    errno = 0;
    assert_null(page_file_create(path));
    assert_int_equal(errno, EEXIST);
    assert_page_holds(0, "kept");
}

static void test_write_and_sync_and_read_report_io_errors([[maybe_unused]] void **state)
{
    page_t page = page_holding("io");

    assert_int_equal(page_file_write(file, 0, &page), 0);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    errno = 0;
    assert_int_equal(page_file_write(file, 1, &page), -1);
    assert_int_equal(errno, EIO);
    io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);
    errno = 0;
    assert_int_equal(page_file_sync(file), -1);
    assert_int_equal(errno, EIO);
    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    assert_read_fails(0, EIO);
}

static void test_a_torn_write_is_a_failure_and_the_page_is_corrupt([[maybe_unused]] void **state)
{
    page_t old = page_holding("old");
    page_t new_page = page_holding("new new new");

    assert_int_equal(page_file_write(file, 0, &old), 0);
    io_init(false);
    io_inject_fault(IO_FAULT_TORN_WRITE, 1);
    errno = 0;
    assert_int_equal(page_file_write(file, 0, &new_page), -1);
    assert_int_equal(errno, EIO);
    io_init(true);
    assert_read_fails(0, EBADMSG);
}

static size_t count_pages(void)
{
    size_t count = SIZE_MAX;

    assert_int_equal(page_file_count_pages(file, &count), 0);
    return count;
}

static void test_the_page_count_follows_the_size_of_the_file([[maybe_unused]] void **state)
{
    page_t page = page_holding("count");

    assert_int_equal(count_pages(), 0);
    assert_int_equal(page_file_write(file, 0, &page), 0);
    assert_int_equal(count_pages(), 1);
    assert_int_equal(page_file_write(file, 4, &page), 0);
    assert_int_equal(count_pages(), 5);
    assert_int_equal(page_file_write(file, 2, &page), 0);
    assert_int_equal(count_pages(), 5);
}

static void test_a_file_that_ends_in_a_page_cannot_be_counted([[maybe_unused]] void **state)
{
    page_t page = page_holding("cut");
    size_t count = 0;

    assert_int_equal(page_file_write(file, 1, &page), 0);
    assert_int_equal(truncate(path, PAGE_SIZE + (PAGE_SIZE / 2)), 0);
    errno = 0;
    assert_int_equal(page_file_count_pages(file, &count), -1);
    assert_int_equal(errno, EBADMSG);
}

static void test_counting_pages_reports_a_stat_error([[maybe_unused]] void **state)
{
    size_t count = 0;

    io_init(false);
    io_inject_fault(IO_FAULT_STAT_ERROR, 1);
    errno = 0;
    assert_int_equal(page_file_count_pages(file, &count), -1);
    assert_int_equal(errno, EIO);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_a_written_page_reads_back_with_its_checksum, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_pages_are_kept_apart, setup, teardown),
    cmocka_unit_test_setup_teardown(test_pages_survive_closing_and_reopening, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_page_that_was_never_written_is_corrupt, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_truncated_page_is_corrupt, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_changed_byte_on_disk_is_caught, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_page_with_a_good_checksum_but_a_bad_structure_is_corrupt,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_open_does_not_create_a_missing_file, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_page_count_follows_the_size_of_the_file, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_file_that_ends_in_a_page_cannot_be_counted, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_counting_pages_reports_a_stat_error, setup, teardown),
    cmocka_unit_test_setup_teardown(test_create_makes_a_new_file, setup, teardown),
    cmocka_unit_test_setup_teardown(test_create_refuses_an_existing_file, setup, teardown),
    cmocka_unit_test_setup_teardown(test_write_and_sync_and_read_report_io_errors, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_torn_write_is_a_failure_and_the_page_is_corrupt, setup,
                                    teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
