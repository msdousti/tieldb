/**
 * Tests that a table uses the engine it was made with, whatever the kind
 *
 * The other engine here is the one-page engine under another name, with counters, so that the
 * tests can tell which factory a table called.
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

#include "storage/catalog.h"
#include "storage/one_page_engine.h"
#include "table_assert.h"

static size_t creates;
static size_t loads;

static storage_engine_t *counting_create(const row_layout_t *layout)
{
    creates++;
    return one_page_engine_create(layout);
}

static storage_engine_t *counting_load(const row_layout_t *layout, const char *path)
{
    loads++;
    return one_page_engine_load(layout, path);
}

static const storage_engine_factory_t COUNTING = {"counting", counting_create, counting_load};
static const table_column_t ID[] = {{"id", &TYPE_INT4}};

static void test_a_table_has_the_default_engine_unless_told_otherwise([[maybe_unused]] void **state)
{
    table_t *table = table_create("plain", ID, 1);

    assert_string_equal(table_engine_name(table), "one_page");
    table_destroy(table);
}

static void test_a_table_is_made_with_the_engine_it_is_given([[maybe_unused]] void **state)
{
    table_t *table = nullptr;

    creates = 0;
    table = table_create_with_engine("counted", ID, 1, &COUNTING);

    assert_int_equal(creates, 1);
    assert_string_equal(table_engine_name(table), "counting");
    table_destroy(table);
}

static void test_the_rows_of_a_table_are_loaded_by_its_own_engine([[maybe_unused]] void **state)
{
    char path[] = "/tmp/tieldb_table_engines_XXXXXX";
    const char *row[] = {"5"};
    table_t *table = table_create_with_engine("reloaded", ID, 1, &COUNTING);

    close(mkstemp(path));
    assert_int_equal(table_insert(table, row, 1), 0);
    assert_int_equal(table_save_rows(table, path), 0);
    loads = 0;
    assert_int_equal(table_load_rows(table, path), 0);

    assert_int_equal(loads, 1);
    assert_cell_text(table, 0, 0, "5");
    unlink(path);
    table_destroy(table);
}

static void test_the_catalog_makes_tables_with_the_given_engine([[maybe_unused]] void **state)
{
    table_t *table = catalog_create_table_with_engine("in_catalog", ID, 1, &COUNTING);

    assert_ptr_equal(catalog_find("in_catalog"), table);
    assert_string_equal(table_engine_name(table), "counting");
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_a_table_has_the_default_engine_unless_told_otherwise),
    cmocka_unit_test(test_a_table_is_made_with_the_engine_it_is_given),
    cmocka_unit_test(test_the_rows_of_a_table_are_loaded_by_its_own_engine),
    cmocka_unit_test(test_the_catalog_makes_tables_with_the_given_engine),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
