#define _POSIX_C_SOURCE 200809L

/**
 * Tests for the buffer pool
 *
 * Each test has a directory with two page files of four pages: the page n of file f holds the
 * text "f:n".
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "buffer/buffer_pool.h"
#include "io_fault.h"
#include "test_directory.h"

static constexpr size_t FRAMES = 3;
static constexpr size_t FILES = 2;
static constexpr size_t PAGES_PER_FILE = 4;

static char directory[] = "/tmp/tieldb_buffer_pool_XXXXXX";
static page_file_t *files[FILES];
static buffer_pool_t *pool;
static size_t file_numbers[FILES];

static void file_path(char *path, size_t size, size_t file)
{
    snprintf(path, size, "%s/file%zu", directory, file);
}

static void write_text(page_file_t *file, size_t page_number, const char *text)
{
    page_t page;
    page_init(&page, PAGE_TYPE_HEAP);
    assert_int_equal(page_insert(&page, text, strlen(text)), 0);
    assert_int_equal(page_file_write(file, page_number, &page), 0);
}

static void fill_file(size_t file)
{
    char text[16];

    for (size_t n = 0; n < PAGES_PER_FILE; n++) {
        snprintf(text, sizeof(text), "%zu:%zu", file, n);
        write_text(files[file], n, text);
    }
}

static void create_file(size_t file)
{
    char path[128];

    file_path(path, sizeof(path), file);
    files[file] = page_file_create(path);
    assert_true(files[file] != nullptr);
    fill_file(file);
    assert_int_equal(buffer_pool_add_file(pool, files[file], &file_numbers[file]), 0);
}

static int setup([[maybe_unused]] void **state)
{
    io_init(true);
    make_directory(directory);
    pool = buffer_pool_create(FRAMES);
    for (size_t file = 0; file < FILES; file++) {
        create_file(file);
    }
    return 0;
}

static int teardown([[maybe_unused]] void **state)
{
    io_init(true);
    assert_int_equal(buffer_pool_destroy(pool), 0);
    for (size_t file = 0; file < FILES; file++) {
        assert_int_equal(page_file_close(files[file]), 0);
    }
    remove_directory(directory);
    strcpy(directory, "/tmp/tieldb_buffer_pool_XXXXXX");
    return 0;
}

static page_id_t id_of(size_t file, size_t page_number)
{
    return (page_id_t){.file_number = file_numbers[file], .page_number = page_number};
}

static page_t *pin(size_t file, size_t page_number)
{
    page_t *page = nullptr;

    assert_int_equal(buffer_pool_pin(pool, id_of(file, page_number), &page), 0);
    return page;
}

static void unpin_with(size_t file, size_t page_number, bool is_changed)
{
    assert_int_equal(buffer_pool_unpin(pool, id_of(file, page_number), is_changed), 0);
}

static void unpin(size_t file, size_t page_number)
{
    unpin_with(file, page_number, false);
}

static void assert_text(const page_t *page, const char *text)
{
    size_t length = 0;
    const void *tuple = page_get(page, 0, &length);

    assert_true(tuple != nullptr);
    assert_int_equal(length, strlen(text));
    assert_memory_equal(tuple, text, length);
}

static void assert_pin_fails(size_t file, size_t page_number, int error)
{
    page_t *page = nullptr;

    errno = 0;
    assert_int_equal(buffer_pool_pin(pool, id_of(file, page_number), &page), -1);
    assert_int_equal(errno, error);
}

static void test_a_pinned_page_holds_what_the_file_holds([[maybe_unused]] void **state)
{
    page_t *page = pin(0, 2);

    assert_text(page, "0:2");
    assert_true(pin(0, 2) == page);
    unpin(0, 2);
    unpin(0, 2);
}

static void test_the_same_page_number_in_two_files_is_two_pages([[maybe_unused]] void **state)
{
    page_t *first = pin(0, 1);
    page_t *second = pin(1, 1);

    assert_true(first != second);
    assert_text(second, "1:1");
    unpin(0, 1);
    unpin(1, 1);
}

static void test_unpinning_what_is_not_pinned_is_refused([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(buffer_pool_unpin(pool, id_of(0, 0), false), -1);
    assert_int_equal(errno, EINVAL);
    assert_true(pin(0, 0) != nullptr);
    unpin(0, 0);
    errno = 0;
    assert_int_equal(buffer_pool_unpin(pool, id_of(0, 0), false), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_a_full_pool_refuses_to_pin_another_page([[maybe_unused]] void **state)
{
    for (size_t n = 0; n < FRAMES; n++) {
        assert_true(pin(0, n) != nullptr);
    }
    assert_pin_fails(0, FRAMES, ENOBUFS);
    for (size_t n = 0; n < FRAMES; n++) {
        unpin(0, n);
    }
}

static void test_a_page_that_cannot_be_read_takes_no_frame([[maybe_unused]] void **state)
{
    for (size_t attempt = 0; attempt < FRAMES + 1; attempt++) {
        assert_pin_fails(0, PAGES_PER_FILE, ENOENT);
    }
    for (size_t n = 0; n < FRAMES; n++) {
        assert_true(pin(0, n) != nullptr);
        unpin(0, n);
    }
}

static void test_a_file_that_was_not_added_is_refused([[maybe_unused]] void **state)
{
    page_t *page = nullptr;

    errno = 0;
    assert_int_equal(buffer_pool_pin(pool, (page_id_t){.file_number = FILES}, &page), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_a_pool_needs_a_frame([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(buffer_pool_create(0));
    assert_int_equal(errno, EINVAL);
}

static void test_destroying_a_pool_with_a_pin_reports_it([[maybe_unused]] void **state)
{
    buffer_pool_t *other = buffer_pool_create(1);

    assert_true(other != nullptr);
    assert_int_equal(buffer_pool_add_file(other, files[0], &(size_t){}), 0);
    assert_int_equal(buffer_pool_pin(other, (page_id_t){}, &(page_t *){}), 0);
    errno = 0;
    assert_int_equal(buffer_pool_destroy(other), -1);
    assert_int_equal(errno, EBUSY);
}

static void change_page(page_t *page)
{
    assert_int_equal(page_insert(page, "new", 3), 1);
}

static void change_and_unpin(size_t file, size_t page_number)
{
    change_page(pin(file, page_number));
    unpin_with(file, page_number, true);
}

static void assert_file_page_slot_count(size_t file, size_t page_number, size_t count)
{
    page_t page;

    assert_int_equal(page_file_read(files[file], page_number, &page), 0);
    assert_int_equal(page_slot_count(&page), count);
}

static void assert_flush_fails(int error)
{
    errno = 0;
    assert_int_equal(buffer_pool_flush_all(pool), -1);
    assert_int_equal(errno, error);
}

static void test_flushing_writes_the_changed_pages_only([[maybe_unused]] void **state)
{
    change_and_unpin(0, 1);
    change_page(pin(0, 2));
    unpin(0, 2);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 1, 2);
    assert_file_page_slot_count(0, 2, 1);
}

static void test_a_page_unpinned_as_changed_stays_changed([[maybe_unused]] void **state)
{
    change_page(pin(0, 0));
    assert_true(pin(0, 0) != nullptr);
    unpin_with(0, 0, true);
    unpin_with(0, 0, false);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 0, 2);
}

static void test_a_flushed_page_is_not_written_again([[maybe_unused]] void **state)
{
    change_and_unpin(0, 0);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
}

static void test_a_failed_write_keeps_the_page_changed([[maybe_unused]] void **state)
{
    change_and_unpin(0, 0);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    assert_flush_fails(EIO);
    io_init(true);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 0, 2);
}

static void test_a_failed_sync_is_reported([[maybe_unused]] void **state)
{
    io_init(false);
    io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);
    assert_flush_fails(EIO);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_a_pinned_page_holds_what_the_file_holds, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_same_page_number_in_two_files_is_two_pages, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_unpinning_what_is_not_pinned_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_full_pool_refuses_to_pin_another_page, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_page_that_cannot_be_read_takes_no_frame, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_file_that_was_not_added_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_pool_needs_a_frame, setup, teardown),
    cmocka_unit_test_setup_teardown(test_destroying_a_pool_with_a_pin_reports_it, setup, teardown),
    cmocka_unit_test_setup_teardown(test_flushing_writes_the_changed_pages_only, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_page_unpinned_as_changed_stays_changed, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_flushed_page_is_not_written_again, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_failed_write_keeps_the_page_changed, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_failed_sync_is_reported, setup, teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
