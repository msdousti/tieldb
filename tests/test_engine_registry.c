/**
 * Tests for the engine registry
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "storage/engine_registry.h"
#include "storage/one_page_engine.h"

static void test_an_engine_is_found_by_its_name([[maybe_unused]] void **state)
{
    assert_ptr_equal(engine_find("one_page"), &ONE_PAGE_ENGINE);
    assert_string_equal(engine_find("one_page")->name, "one_page");
}

static void test_an_unknown_engine_is_not_found([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(engine_find("lsm"));
    assert_int_equal(errno, ENOENT);
    assert_null(engine_find(""));
}

static void test_the_default_engine_is_the_one_page_engine([[maybe_unused]] void **state)
{
    assert_ptr_equal(engine_default(), &ONE_PAGE_ENGINE);
}

static size_t count_rows_of(storage_engine_t *engine)
{
    storage_cursor_t *cursor = storage_open_cursor(engine);
    row_value_t values[1];
    size_t count = 0;

    while (storage_next(cursor, values) == 1) {
        count++;
    }
    storage_close_cursor(cursor);
    return count;
}

static void test_a_factory_makes_a_working_engine([[maybe_unused]] void **state)
{
    storage_engine_t *engine = engine_find("one_page")->create(&(row_layout_t){nullptr, 0});

    assert_int_equal(storage_insert(engine, nullptr), 0);
    assert_int_equal(count_rows_of(engine), 1);
    storage_destroy(engine);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_an_engine_is_found_by_its_name),
    cmocka_unit_test(test_an_unknown_engine_is_not_found),
    cmocka_unit_test(test_the_default_engine_is_the_one_page_engine),
    cmocka_unit_test(test_a_factory_makes_a_working_engine),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
