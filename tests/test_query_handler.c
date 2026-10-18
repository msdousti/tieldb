/**
 * Tests for the hardcoded simple-query handler
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "net/query_handler.h"
#include "storage/catalog.h"

static wire_writer_t *create_writer(void)
{
    wire_writer_t *writer = wire_writer_create();
    if (writer == nullptr) {
        fail();
    }
    return writer;
}

static void put_expected_version_response(wire_writer_t *w)
{
    const protocol_column_t column = {"version", PROTOCOL_TYPE_TEXT};
    const protocol_value_t value = {"TielDB 0.1.0", strlen("TielDB 0.1.0")};

    assert_int_equal(protocol_put_row_description(w, &column, 1), 0);
    assert_int_equal(protocol_put_data_row(w, &value, 1), 0);
    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_unsupported_response(wire_writer_t *w)
{
    assert_int_equal(
        protocol_put_error_response(
            w, "ERROR", "0A000",
            "only SELECT version(), SELECT * FROM <table>, INSERT INTO <table> VALUES (...) and "
            "CREATE TABLE are supported"),
        0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_user_rows(wire_writer_t *w, size_t count)
{
    const protocol_column_t columns[] = {{"id", PROTOCOL_TYPE_INT4}, {"name", PROTOCOL_TYPE_TEXT}};
    const char *rows[][2] = {{"1", "Alice"}, {"2", "Bob"}, {"3", "Carol"}, {"4", "Dave"}};
    char tag[16];

    assert_int_equal(protocol_put_row_description(w, columns, 2), 0);
    for (size_t i = 0; i < count; i++) {
        const protocol_value_t values[] = {{rows[i][0], strlen(rows[i][0])},
                                           {rows[i][1], strlen(rows[i][1])}};
        assert_int_equal(protocol_put_data_row(w, values, 2), 0);
    }
    snprintf(tag, sizeof(tag), "SELECT %zu", count);
    assert_int_equal(protocol_put_command_complete(w, tag), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_user_response(wire_writer_t *w)
{
    put_expected_user_rows(w, 3);
}

static void put_expected_user_with_dave_response(wire_writer_t *w)
{
    put_expected_user_rows(w, 4);
}

static void put_expected_insert_response(wire_writer_t *w)
{
    assert_int_equal(protocol_put_command_complete(w, "INSERT 0 1"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_wrong_count_response(wire_writer_t *w)
{
    assert_int_equal(protocol_put_error_response(w, "ERROR", "42601",
                                                 "INSERT must supply a value for every column"),
                     0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_missing_table_response(wire_writer_t *w)
{
    assert_int_equal(
        protocol_put_error_response(w, "ERROR", "42P01", "relation \"nope\" does not exist"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void assert_response(const char *sql, void (*put_expected)(wire_writer_t *))
{
    wire_writer_t *actual = create_writer();
    wire_writer_t *expected = create_writer();

    put_expected(expected);
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, actual), 0);

    assert_int_equal(wire_writer_size(actual), wire_writer_size(expected));
    assert_memory_equal(wire_writer_data(actual), wire_writer_data(expected),
                        wire_writer_size(expected));
    wire_writer_destroy(actual);
    wire_writer_destroy(expected);
}

static void test_version_query_returns_one_row([[maybe_unused]] void **state)
{
    assert_response("SELECT version()", put_expected_version_response);
}

static void test_version_query_ignores_case_semicolon_and_spaces([[maybe_unused]] void **state)
{
    assert_response("select VERSION();", put_expected_version_response);
    assert_response("  SELECT version()  ;  ", put_expected_version_response);
    assert_response("\nselect version()\n", put_expected_version_response);
}

static void test_select_star_returns_every_row_of_the_table([[maybe_unused]] void **state)
{
    assert_response("SELECT * FROM user", put_expected_user_response);
    assert_response("select*from USER ;", put_expected_user_response);
}

static void test_select_star_from_a_missing_table_is_an_error([[maybe_unused]] void **state)
{
    assert_response("SELECT * FROM nope", put_expected_missing_table_response);
}

static void test_insert_with_a_wrong_value_count_is_an_error([[maybe_unused]] void **state)
{
    assert_response("INSERT INTO user VALUES (4)", put_expected_wrong_count_response);
}

static void put_expected_create_table_response(wire_writer_t *w)
{
    assert_int_equal(protocol_put_command_complete(w, "CREATE TABLE"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_table_exists_response(wire_writer_t *w)
{
    assert_int_equal(
        protocol_put_error_response(w, "ERROR", "42P07", "relation \"user\" already exists"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void put_expected_pets_response(wire_writer_t *w)
{
    const protocol_column_t columns[] = {{"id", PROTOCOL_TYPE_INT4}, {"name", PROTOCOL_TYPE_TEXT}};
    const protocol_value_t values[] = {{"7", 1}, {"Rex", 3}};

    assert_int_equal(protocol_put_row_description(w, columns, 2), 0);
    assert_int_equal(protocol_put_data_row(w, values, 2), 0);
    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void test_create_table_insert_and_select_work_together([[maybe_unused]] void **state)
{
    assert_response("CREATE TABLE pets (id INTEGER, name TEXT);",
                    put_expected_create_table_response);
    assert_response("INSERT INTO pets VALUES (7, 'Rex')", put_expected_insert_response);
    assert_response("SELECT * FROM pets", put_expected_pets_response);
}

static void put_expected_rows_without_columns_response(wire_writer_t *w)
{
    assert_int_equal(protocol_put_row_description(w, nullptr, 0), 0);
    assert_int_equal(protocol_put_data_row(w, nullptr, 0), 0);
    assert_int_equal(protocol_put_data_row(w, nullptr, 0), 0);
    assert_int_equal(protocol_put_command_complete(w, "SELECT 2"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void test_table_without_columns_can_be_created_and_selected([[maybe_unused]] void **state)
{
    const char *no_cells[1] = {};

    assert_response("create table empty_rows();", put_expected_create_table_response);
    table_t *table = catalog_find("empty_rows");
    assert_int_equal(table_insert(table, no_cells, 0), 0);
    assert_int_equal(table_insert(table, no_cells, 0), 0);

    assert_response("SELECT * FROM empty_rows", put_expected_rows_without_columns_response);
}

static void test_create_table_for_an_existing_table_is_an_error([[maybe_unused]] void **state)
{
    assert_response("CREATE TABLE user (id INTEGER)", put_expected_table_exists_response);
}

static void test_other_queries_get_an_error_and_stay_usable([[maybe_unused]] void **state)
{
    assert_response("SELECT 1", put_expected_unsupported_response);
    assert_response("SELECT version() FROM t", put_expected_unsupported_response);
    assert_response("", put_expected_unsupported_response);
}

static void test_response_appends_after_existing_content([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const char *sql = "SELECT 1";

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w), 0);
    assert_int_equal(wire_writer_data(w)[0], 'R');
    assert_int_equal(wire_writer_data(w)[9], 'E');
    wire_writer_destroy(w);
}

static void test_message_without_a_string_is_rejected([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    errno = 0;
    assert_int_equal(query_handle_message("", 0, w), -1);
    assert_int_equal(errno, EBADMSG);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_null_writer_is_rejected([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(query_handle_message("SELECT 1", sizeof("SELECT 1"), nullptr), -1);
    assert_int_equal(errno, EINVAL);
}

static void put_expected_flipped_response(wire_writer_t *w)
{
    const protocol_column_t columns[] = {{"b", PROTOCOL_TYPE_TEXT}, {"a", PROTOCOL_TYPE_INT4}};
    const protocol_value_t values[] = {{"one", 3}, {"1", 1}};

    assert_int_equal(protocol_put_row_description(w, columns, 2), 0);
    assert_int_equal(protocol_put_data_row(w, values, 2), 0);
    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void test_select_star_shows_columns_in_the_display_order([[maybe_unused]] void **state)
{
    const table_column_t columns[] = {{"a", COLUMN_TYPE_INTEGER}, {"b", COLUMN_TYPE_TEXT}};
    const char *row[] = {"1", "one"};
    const size_t flipped[] = {1, 0};
    table_t *table = catalog_create_table("shown_flipped", columns, 2);
    if (table == nullptr) {
        fail();
    }
    assert_int_equal(table_insert(table, row, 2), 0);
    assert_int_equal(table_set_display_order(table, flipped, 2), 0);

    assert_response("SELECT * FROM shown_flipped", put_expected_flipped_response);
}

// Runs last: it adds a row to the user table, which the other tests count
static void test_inserted_row_shows_up_in_select_star([[maybe_unused]] void **state)
{
    assert_response("INSERT INTO user VALUES (4, 'Dave');", put_expected_insert_response);
    assert_response("SELECT * FROM user", put_expected_user_with_dave_response);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_version_query_returns_one_row),
    cmocka_unit_test(test_version_query_ignores_case_semicolon_and_spaces),
    cmocka_unit_test(test_select_star_returns_every_row_of_the_table),
    cmocka_unit_test(test_select_star_from_a_missing_table_is_an_error),
    cmocka_unit_test(test_select_star_shows_columns_in_the_display_order),
    cmocka_unit_test(test_insert_with_a_wrong_value_count_is_an_error),
    cmocka_unit_test(test_create_table_insert_and_select_work_together),
    cmocka_unit_test(test_table_without_columns_can_be_created_and_selected),
    cmocka_unit_test(test_create_table_for_an_existing_table_is_an_error),
    cmocka_unit_test(test_other_queries_get_an_error_and_stay_usable),
    cmocka_unit_test(test_response_appends_after_existing_content),
    cmocka_unit_test(test_message_without_a_string_is_rejected),
    cmocka_unit_test(test_null_writer_is_rejected),
    cmocka_unit_test(test_inserted_row_shows_up_in_select_star),
};

// The tests share one table: user (id INTEGER, name TEXT) with three rows
static int create_user_table([[maybe_unused]] void **state)
{
    const table_column_t columns[] = {{"id", COLUMN_TYPE_INTEGER}, {"name", COLUMN_TYPE_TEXT}};
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
