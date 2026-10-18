/**
 * Tests for statement execution
 *
 * The statements run against the built-in user table, which is shared by every test in this
 * file, so the test that really inserts a row comes last.
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "executor/executor.h"
#include "storage/table.h"

static void run_insert(const char *sql, executor_error_t *error)
{
    static char scratch[256];
    sql_insert_t insert;

    assert_true(sql_parse_insert(sql, scratch, &insert));
    assert_int_equal(executor_insert(&insert, error), 0);
}

static void assert_rejected(const char *sql, const char *sqlstate, const char *message)
{
    executor_error_t error;
    size_t rows = table_row_count(table_find("user"));

    run_insert(sql, &error);

    assert_string_equal(error.sqlstate, sqlstate);
    assert_string_equal(error.message, message);
    assert_int_equal(table_row_count(table_find("user")), rows);
}

static void test_insert_into_a_missing_table_is_an_error([[maybe_unused]] void **state)
{
    assert_rejected("INSERT INTO nope VALUES (1, 'x')", "42P01",
                    "relation \"nope\" does not exist");
}

static void test_insert_with_the_wrong_number_of_values_is_an_error([[maybe_unused]] void **state)
{
    const char *message = "INSERT must supply a value for every column";

    assert_rejected("INSERT INTO user VALUES (1)", "42601", message);
    assert_rejected("INSERT INTO user VALUES (1, 'x', 'y')", "42601", message);
}

static void test_text_in_an_integer_column_is_an_error([[maybe_unused]] void **state)
{
    const char *syntax = "invalid input syntax for type integer: \"abc\"";

    assert_rejected("INSERT INTO user VALUES ('abc', 'x')", "22P02", syntax);
    assert_rejected("INSERT INTO user VALUES ('12x', 'x')", "22P02",
                    "invalid input syntax for type integer: \"12x\"");
    assert_rejected("INSERT INTO user VALUES ('', 'x')", "22P02",
                    "invalid input syntax for type integer: \"\"");
}

static void test_integers_outside_32_bits_are_an_error([[maybe_unused]] void **state)
{
    assert_rejected("INSERT INTO user VALUES (3000000000, 'x')", "22P02",
                    "invalid input syntax for type integer: \"3000000000\"");
    assert_rejected("INSERT INTO user VALUES (-3000000000, 'x')", "22P02",
                    "invalid input syntax for type integer: \"-3000000000\"");
    assert_rejected("INSERT INTO user VALUES (99999999999999999999, 'x')", "22P02",
                    "invalid input syntax for type integer: \"99999999999999999999\"");
}

static void test_valid_insert_adds_a_row([[maybe_unused]] void **state)
{
    executor_error_t error;
    const table_t *user = table_find("user");

    run_insert("INSERT INTO user VALUES ('4', 'Dave')", &error);

    assert_string_equal(error.sqlstate, "");
    assert_int_equal(table_row_count(user), 4);
    assert_string_equal(table_cell(user, 3, 0), "4");
    assert_string_equal(table_cell(user, 3, 1), "Dave");
}

static void test_number_in_a_text_column_is_stored_as_text([[maybe_unused]] void **state)
{
    executor_error_t error;
    const table_t *user = table_find("user");

    run_insert("INSERT INTO user VALUES (5, 42)", &error);

    assert_string_equal(error.sqlstate, "");
    assert_string_equal(table_cell(user, 4, 1), "42");
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_insert_into_a_missing_table_is_an_error),
    cmocka_unit_test(test_insert_with_the_wrong_number_of_values_is_an_error),
    cmocka_unit_test(test_text_in_an_integer_column_is_an_error),
    cmocka_unit_test(test_integers_outside_32_bits_are_an_error),
    cmocka_unit_test(test_valid_insert_adds_a_row),
    cmocka_unit_test(test_number_in_a_text_column_is_stored_as_text),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
