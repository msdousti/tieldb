/**
 * Tests for the hardcoded simple-query handler
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "net/query_handler.h"

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
        protocol_put_error_response(w, "ERROR", "0A000", "only SELECT version() is supported"), 0);
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

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_version_query_returns_one_row),
    cmocka_unit_test(test_version_query_ignores_case_semicolon_and_spaces),
    cmocka_unit_test(test_other_queries_get_an_error_and_stay_usable),
    cmocka_unit_test(test_response_appends_after_existing_content),
    cmocka_unit_test(test_message_without_a_string_is_rejected),
    cmocka_unit_test(test_null_writer_is_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
