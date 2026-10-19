/**
 * Tests for saving the one-page engine to a file and loading it again
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

#include "storage/one_page_engine.h"
#include "storage/page_file.h"

static const type_info_t *const INT_TEXT[] = {&TYPE_INT4, &TYPE_TEXT};
static const row_layout_t PAIR = {INT_TEXT, 2};
static const char PATH_TEMPLATE[] = "/tmp/tieldb_engine_file_XXXXXX";
static char path[sizeof(PATH_TEMPLATE)];

static int setup([[maybe_unused]] void **state)
{
    memcpy(path, PATH_TEMPLATE, sizeof(path));
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return unlink(path);
}

static int teardown([[maybe_unused]] void **state)
{
    unlink(path);
    return 0;
}

static int insert_numbered_row(storage_engine_t *engine, int32_t number)
{
    const char text[] = "row";
    const row_value_t values[] = {{&number, sizeof(number)}, {text, sizeof(text) - 1}};

    return storage_insert(engine, values);
}

// Inserts count rows, or as many as fit in the page
static storage_engine_t *engine_with_rows(size_t count)
{
    storage_engine_t *engine = one_page_engine_create(&PAIR);
    int status = 0;

    for (size_t i = 0; i < count && status == 0; i++) {
        status = insert_numbered_row(engine, (int32_t) i);
    }
    assert_true(status == 0 || errno == ENOSPC);
    return engine;
}

static size_t assert_rows_are_in_order(storage_engine_t *engine)
{
    storage_cursor_t *cursor = storage_open_cursor(engine);
    row_value_t values[2];
    int32_t number = 0;
    size_t count = 0;

    while (storage_next(cursor, values) == 1) {
        memcpy(&number, values[0].data, sizeof(number));
        assert_int_equal(number, count);
        assert_memory_equal(values[1].data, "row", 3);
        count++;
    }
    storage_close_cursor(cursor);
    return count;
}

static storage_engine_t *save_and_load(storage_engine_t *saved)
{
    storage_engine_t *loaded = nullptr;

    assert_int_equal(storage_save(saved, path), 0);
    loaded = one_page_engine_load(&PAIR, path);
    if (loaded == nullptr) {
        fail();
    }
    return loaded;
}

static void test_saved_rows_come_back_when_the_engine_is_loaded([[maybe_unused]] void **state)
{
    storage_engine_t *saved = engine_with_rows(50);
    storage_engine_t *loaded = save_and_load(saved);

    assert_int_equal(assert_rows_are_in_order(loaded), 50);
    storage_destroy(saved);
    storage_destroy(loaded);
}

static void test_a_full_engine_and_a_second_save_replace_the_file([[maybe_unused]] void **state)
{
    storage_engine_t *few = engine_with_rows(3);
    storage_engine_t *full = engine_with_rows(100000);
    storage_engine_t *loaded = nullptr;

    assert_int_equal(storage_save(few, path), 0);
    loaded = save_and_load(full);
    assert_int_equal(assert_rows_are_in_order(loaded), assert_rows_are_in_order(full));
    storage_destroy(few);
    storage_destroy(full);
    storage_destroy(loaded);
}

static void test_a_file_that_cannot_be_loaded_is_reported([[maybe_unused]] void **state)
{
    storage_engine_t *saved = engine_with_rows(5);
    int fd = -1;
    char byte = 0;

    errno = 0;
    assert_null(one_page_engine_load(&PAIR, path));
    assert_int_equal(errno, ENOENT);
    assert_int_equal(storage_save(saved, path), 0);
    fd = open(path, O_RDWR);
    assert_int_equal(pread(fd, &byte, 1, PAGE_SIZE - 1), 1);
    byte ^= 0x01;
    assert_int_equal(pwrite(fd, &byte, 1, PAGE_SIZE - 1), 1);
    close(fd);
    errno = 0;
    assert_null(one_page_engine_load(&PAIR, path));
    assert_int_equal(errno, EBADMSG);
    storage_destroy(saved);
}

static void test_a_file_of_another_kind_is_refused([[maybe_unused]] void **state)
{
    page_t page;

    page_init(&page, PAGE_TYPE_CATALOG);
    assert_int_equal(page_file_save(path, &page), 0);
    errno = 0;
    assert_null(one_page_engine_load(&PAIR, path));
    assert_int_equal(errno, EBADMSG);
}

static void test_saving_to_a_path_that_cannot_be_created_fails([[maybe_unused]] void **state)
{
    storage_engine_t *saved = engine_with_rows(1);

    errno = 0;
    assert_int_equal(storage_save(saved, "/tmp/tieldb_no_such_directory/table"), -1);
    assert_int_equal(errno, ENOENT);
    storage_destroy(saved);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_saved_rows_come_back_when_the_engine_is_loaded, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_full_engine_and_a_second_save_replace_the_file, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_file_that_cannot_be_loaded_is_reported, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_file_of_another_kind_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_saving_to_a_path_that_cannot_be_created_fails, setup,
                                    teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
