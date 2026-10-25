/**
 * Tests for files that hold exactly one page
 *
 * Each test works on a path that the setup makes and the teardown removes.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "io/io.h"
#include "io_fault.h"
#include "storage/one_page_file.h"

static const char PATH_TEMPLATE[] = "/tmp/tieldb_one_page_file_XXXXXX";
static char path[sizeof(PATH_TEMPLATE)];

static int setup([[maybe_unused]] void **state)
{
    memcpy(path, PATH_TEMPLATE, sizeof(path));
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    io_init(true);
    return 0;
}

static int teardown([[maybe_unused]] void **state)
{
    io_init(true);
    unlink(path);
    return 0;
}

static page_t page_holding(const char *text)
{
    page_t page;
    page_init(&page, PAGE_TYPE_HEAP);
    assert_int_equal(page_insert(&page, text, strlen(text)), 0);
    return page;
}

static void test_save_creates_or_replaces_a_file_and_load_reads_it([[maybe_unused]] void **state)
{
    page_t first = page_holding("first");
    page_t second = page_holding("second");
    page_t loaded;

    assert_int_equal(unlink(path), 0);
    assert_int_equal(one_page_file_save(path, &first), 0);
    assert_int_equal(one_page_file_save(path, &second), 0);
    assert_int_equal(one_page_file_load(path, &loaded), 0);
    assert_memory_equal(page_get(&loaded, 0, &(size_t){0}), "second", 6);
}

static void test_load_reports_a_missing_or_empty_file([[maybe_unused]] void **state)
{
    page_t page;

    errno = 0;
    assert_int_equal(one_page_file_load("/tmp/tieldb_no_such_directory/pages", &page), -1);
    assert_int_equal(errno, ENOENT);
    assert_int_equal(truncate(path, 0), 0);
    errno = 0;
    assert_int_equal(one_page_file_load(path, &page), -1);
    assert_int_equal(errno, ENOENT);
}

static void test_save_reports_its_failures([[maybe_unused]] void **state)
{
    page_t page = page_holding("x");

    errno = 0;
    assert_int_equal(one_page_file_save("/tmp/tieldb_no_such_directory/pages", &page), -1);
    assert_int_equal(errno, ENOENT);
    errno = 0;
    assert_int_equal(one_page_file_save("/dev/null/pages", &page), -1);
    assert_int_equal(errno, ENOTDIR);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    assert_int_equal(one_page_file_save(path, &page), -1);
    io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);
    assert_int_equal(one_page_file_save(path, &page), -1);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_save_creates_or_replaces_a_file_and_load_reads_it, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_load_reports_a_missing_or_empty_file, setup, teardown),
    cmocka_unit_test_setup_teardown(test_save_reports_its_failures, setup, teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
