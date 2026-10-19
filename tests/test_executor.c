/**
 * Tests for statement execution
 *
 * The statements run against the user table that create_user_table makes, which is shared by
 * every test in this file, so the tests that really insert a row come after the ones that count
 * rows.
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "executor/executor.h"
#include "storage/catalog.h"
#include "table_assert.h"

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
    size_t rows = count_rows(catalog_find("user"));

    run_insert(sql, &error);

    assert_string_equal(error.sqlstate, sqlstate);
    assert_string_equal(error.message, message);
    assert_int_equal(count_rows(catalog_find("user")), rows);
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
    const table_t *user = catalog_find("user");

    run_insert("INSERT INTO user VALUES ('4', 'Dave')", &error);

    assert_string_equal(error.sqlstate, "");
    assert_int_equal(count_rows(user), 4);
    assert_cell_text(user, 3, 0, "4");
    assert_cell_text(user, 3, 1, "Dave");
}

static void test_number_in_a_text_column_is_stored_as_text([[maybe_unused]] void **state)
{
    executor_error_t error;
    const table_t *user = catalog_find("user");

    run_insert("INSERT INTO user VALUES (5, 42)", &error);

    assert_string_equal(error.sqlstate, "");
    assert_cell_text(user, 4, 1, "42");
}

static void test_select_from_a_missing_table_is_an_error([[maybe_unused]] void **state)
{
    executor_select_t select;

    executor_select_all("nope", &select);

    assert_null(select.table);
    assert_string_equal(select.error.sqlstate, "42P01");
    assert_string_equal(select.error.message, "relation \"nope\" does not exist");
    executor_select_release(&select);
}

static void test_select_gives_the_locked_table_until_released([[maybe_unused]] void **state)
{
    executor_select_t select;
    const char *row[] = {"9", "Zed"};

    executor_select_all("user", &select);

    assert_string_equal(select.error.sqlstate, "");
    if (select.table == nullptr) {
        fail();
    }
    assert_int_equal(table_column_count(select.table), 2);
    executor_select_release(&select);
    assert_null(select.table);
    assert_int_equal(table_insert(catalog_find("user"), row, 2), 0);
}

static void test_insert_values_follow_the_display_order([[maybe_unused]] void **state)
{
    const table_column_t columns[] = {{"a", &TYPE_INT4}, {"b", &TYPE_TEXT}};
    const size_t flipped[] = {1, 0};
    executor_error_t error;
    table_t *table = catalog_create_table("flipped", columns, 2);
    if (table == nullptr) {
        fail();
    }
    assert_int_equal(table_set_display_order(table, flipped, 2), 0);

    run_insert("INSERT INTO flipped VALUES ('x', 5)", &error);

    assert_string_equal(error.sqlstate, "");
    assert_cell_text(table, 0, 0, "5");
    assert_cell_text(table, 0, 1, "x");
}

static void test_integer_check_uses_the_column_shown_at_that_position([[maybe_unused]] void **state)
{
    const size_t flipped[] = {1, 0};
    executor_error_t error;
    table_t *table = catalog_find("flipped");

    assert_int_equal(table_set_display_order(table, flipped, 2), 0);
    run_insert("INSERT INTO flipped VALUES (5, 'x')", &error);

    assert_string_equal(error.sqlstate, "22P02");
    assert_string_equal(error.message, "invalid input syntax for type integer: \"x\"");
    assert_int_equal(count_rows(table), 1);
}

static void run_create(const char *sql, executor_error_t *error)
{
    sql_create_table_t create;

    assert_true(sql_parse_create_table(sql, &create));
    assert_int_equal(executor_create_table(&create, error), 0);
}

static table_t *create_text_table(const char *name)
{
    const table_column_t columns[] = {{"t", &TYPE_TEXT}};
    table_t *table = catalog_create_table(name, columns, 1);
    if (table == nullptr) {
        fail();
    }
    return table;
}

static size_t insert_until_refused(const char *sql, executor_error_t *error)
{
    size_t rows = 0;

    do {
        run_insert(sql, error);
    } while (error->sqlstate[0] == '\0' && ++rows < 10000);
    return rows;
}

static void test_insert_into_a_full_table_is_an_error([[maybe_unused]] void **state)
{
    table_t *table = create_text_table("full_table");
    executor_error_t error;
    size_t rows = insert_until_refused("INSERT INTO full_table VALUES ('0123456789')", &error);

    assert_string_equal(error.sqlstate, "53100");
    assert_string_equal(error.message, "table is full");
    assert_true(rows > 100);
    assert_int_equal(count_rows(table), rows);
}

static void test_insert_of_a_row_too_big_for_a_table_is_an_error([[maybe_unused]] void **state)
{
    static char sql[10000];
    static char scratch[10000];
    table_t *table = create_text_table("big_row_table");
    sql_insert_t insert;
    executor_error_t error;

    snprintf(sql, sizeof(sql), "INSERT INTO big_row_table VALUES ('%09000d')", 7);
    assert_true(sql_parse_insert(sql, scratch, &insert));
    assert_int_equal(executor_insert(&insert, &error), 0);

    assert_string_equal(error.sqlstate, "54000");
    assert_string_equal(error.message, "row is too big");
    assert_int_equal(count_rows(table), 0);
}

static void test_create_table_makes_a_table_with_the_given_columns([[maybe_unused]] void **state)
{
    executor_error_t error;

    run_create("CREATE TABLE made (a INTEGER, b TEXT, c INT, d int4)", &error);

    const table_t *made = catalog_find("made");
    assert_string_equal(error.sqlstate, "");
    assert_int_equal(table_column_count(made), 4);
    assert_string_equal(table_column(made, 2)->name, "c");
    assert_ptr_equal(table_column(made, 0)->type, &TYPE_INT4);
    assert_ptr_equal(table_column(made, 1)->type, &TYPE_TEXT);
    assert_ptr_equal(table_column(made, 2)->type, &TYPE_INT4);
    assert_ptr_equal(table_column(made, 3)->type, &TYPE_INT4);
    assert_int_equal(count_rows(made), 0);
}

static void test_create_table_without_columns_makes_an_empty_table([[maybe_unused]] void **state)
{
    executor_error_t error;

    run_create("CREATE TABLE nothing ()", &error);

    assert_string_equal(error.sqlstate, "");
    assert_int_equal(table_column_count(catalog_find("nothing")), 0);
}

static void assert_create_rejected(const char *sql, const char *sqlstate, const char *message)
{
    executor_error_t error;

    run_create(sql, &error);

    assert_string_equal(error.sqlstate, sqlstate);
    assert_string_equal(error.message, message);
}

static void test_create_table_with_an_unknown_type_is_an_error([[maybe_unused]] void **state)
{
    assert_create_rejected("CREATE TABLE odd (a float)", "42704", "type \"float\" does not exist");
    assert_null(catalog_find("odd"));
}

static void test_create_table_with_a_repeated_column_is_an_error([[maybe_unused]] void **state)
{
    assert_create_rejected("CREATE TABLE twice (a int, b text, a text)", "42701",
                           "column \"a\" specified more than once");
    assert_null(catalog_find("twice"));
}

static void test_create_table_for_an_existing_table_is_an_error([[maybe_unused]] void **state)
{
    assert_create_rejected("CREATE TABLE user (a int)", "42P07",
                           "relation \"user\" already exists");
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_insert_into_a_missing_table_is_an_error),
    cmocka_unit_test(test_insert_with_the_wrong_number_of_values_is_an_error),
    cmocka_unit_test(test_text_in_an_integer_column_is_an_error),
    cmocka_unit_test(test_integers_outside_32_bits_are_an_error),
    cmocka_unit_test(test_select_from_a_missing_table_is_an_error),
    cmocka_unit_test(test_valid_insert_adds_a_row),
    cmocka_unit_test(test_number_in_a_text_column_is_stored_as_text),
    cmocka_unit_test(test_select_gives_the_locked_table_until_released),
    cmocka_unit_test(test_insert_into_a_full_table_is_an_error),
    cmocka_unit_test(test_insert_of_a_row_too_big_for_a_table_is_an_error),
    cmocka_unit_test(test_create_table_makes_a_table_with_the_given_columns),
    cmocka_unit_test(test_create_table_without_columns_makes_an_empty_table),
    cmocka_unit_test(test_create_table_with_an_unknown_type_is_an_error),
    cmocka_unit_test(test_create_table_with_a_repeated_column_is_an_error),
    cmocka_unit_test(test_create_table_for_an_existing_table_is_an_error),
    cmocka_unit_test(test_insert_values_follow_the_display_order),
    cmocka_unit_test(test_integer_check_uses_the_column_shown_at_that_position),
};

// The tests share one table: user (id INTEGER, name TEXT) with three rows
static int create_user_table([[maybe_unused]] void **state)
{
    const table_column_t columns[] = {{"id", &TYPE_INT4}, {"name", &TYPE_TEXT}};
    const char *rows[][2] = {{"1", "Alice"}, {"2", "Bob"}, {"3", "Carol"}};
    table_t *table = catalog_create_table("user", columns, 2);
    int status = table == nullptr;

    for (size_t i = 0; i < 3 && status == 0; i++) {
        status = table_insert(table, rows[i], 2);
    }
    return status;
}

int main(void)
{
    return cmocka_run_group_tests(tests, create_user_table, nullptr);
}
