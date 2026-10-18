/**
 * Tests for the in-memory tables
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "storage/table.h"

static void test_unknown_table_is_not_found([[maybe_unused]] void **state)
{
    assert_null(table_find("nope"));
}

static void assert_column_is(const table_t *table, size_t index, const char *name,
                             column_type_t type)
{
    assert_string_equal(table_column(table, index)->name, name);
    assert_int_equal(table_column(table, index)->type, type);
}

static void test_user_table_has_two_columns([[maybe_unused]] void **state)
{
    const table_t *user = table_find("user");

    assert_int_equal(table_column_count(user), 2);
    assert_column_is(user, 0, "id", COLUMN_TYPE_INTEGER);
    assert_column_is(user, 1, "name", COLUMN_TYPE_TEXT);
}

static void test_user_table_has_three_rows([[maybe_unused]] void **state)
{
    const table_t *user = table_find("user");

    assert_int_equal(table_row_count(user), 3);
    assert_string_equal(table_cell(user, 0, 0), "1");
    assert_string_equal(table_cell(user, 0, 1), "Alice");
    assert_string_equal(table_cell(user, 2, 0), "3");
    assert_string_equal(table_cell(user, 2, 1), "Carol");
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_unknown_table_is_not_found),
    cmocka_unit_test(test_user_table_has_two_columns),
    cmocka_unit_test(test_user_table_has_three_rows),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
