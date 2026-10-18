/**
 * Tests for Postgres backend message encoding
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "net/protocol.h"

static wire_writer_t *create_writer(void)
{
    wire_writer_t *writer = wire_writer_create();
    if (writer == nullptr) {
        fail();
    }
    return writer;
}

static void assert_writer_holds(const wire_writer_t *writer, const uint8_t *expected, size_t size)
{
    assert_int_equal(wire_writer_size(writer), size);
    assert_memory_equal(wire_writer_data(writer), expected, size);
}

static void test_authentication_ok_matches_postgres_bytes([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'R', 0, 0, 0, 8, 0, 0, 0, 0};

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_ready_for_query_encodes_each_status([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'Z', 0, 0, 0, 5, 'I', 'Z', 0, 0, 0, 5, 'T', 'Z', 0, 0, 0, 5, 'E'};

    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IN_BLOCK), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_FAILED), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_messages_append_after_existing_content([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_ready_for_query_rejects_unknown_status([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    errno = 0;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): the invalid value is the test
    assert_int_equal(protocol_put_ready_for_query(w, (transaction_status_t) 'X'), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_encoders_reject_null_writer([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(protocol_put_authentication_ok(nullptr), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(protocol_put_ready_for_query(nullptr, TRANSACTION_IDLE), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_command_complete_matches_postgres_bytes([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'C', 0, 0, 0, 13, 'S', 'E', 'L', 'E', 'C', 'T', ' ', '1', 0};

    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_command_complete_length_ignores_earlier_content([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'R', 0, 0, 0, 8, 0, 0, 0, 0, 'C', 0, 0, 0, 6, 'X', 0};

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_command_complete(w, "X"), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_command_complete_rejects_null_arguments([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    errno = 0;
    assert_int_equal(protocol_put_command_complete(nullptr, "SELECT 1"), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(protocol_put_command_complete(w, nullptr), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_error_response_matches_postgres_bytes([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'E', 0,   0,   0,   33,  'S', 'E', 'R', 'R', 'O', 'R', 0,
                                'C', '4', '2', '6', '0', '1', 0,   'M', 's', 'y', 'n', 't',
                                'a', 'x', ' ', 'e', 'r', 'r', 'o', 'r', 0,   0};

    assert_int_equal(protocol_put_error_response(w, "ERROR", "42601", "syntax error"), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_error_response_length_ignores_earlier_content([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_error_response(w, "FATAL", "28000", "x"), 0);
    assert_int_equal(wire_writer_size(w), 9 + 1 + 4 + 7 + 7 + 3 + 1);
    assert_int_equal(wire_writer_data(w)[13], 22);
    wire_writer_destroy(w);
}

static void test_error_response_rejects_invalid_arguments([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const char *const sqlstates[] = {"4260", "426011"};

    errno = 0;
    assert_int_equal(protocol_put_error_response(nullptr, "ERROR", "42601", "m"), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(protocol_put_error_response(w, nullptr, "42601", "m"), -1);
    assert_int_equal(protocol_put_error_response(w, "ERROR", nullptr, "m"), -1);
    assert_int_equal(protocol_put_error_response(w, "ERROR", "42601", nullptr), -1);
    for (size_t i = 0; i < 2; i++) {
        errno = 0;
        assert_int_equal(protocol_put_error_response(w, "ERROR", sqlstates[i], "m"), -1);
        assert_int_equal(errno, EINVAL);
    }
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_row_description_matches_postgres_bytes([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const protocol_column_t columns[] = {{"id", PROTOCOL_TYPE_INT4}};
    const uint8_t expected[] = {
        'T',  0,    0,    0,    27, // type, length
        0,    1,                    // one column
        'i',  'd',  0,              // name
        0,    0,    0,    0,        // table OID
        0,    0,                    // attribute number
        0,    0,    0,    23,       // type OID (int4)
        0,    4,                    // type size
        0xFF, 0xFF, 0xFF, 0xFF,     // type modifier (-1)
        0,    0,                    // format code (text)
    };

    assert_int_equal(protocol_put_row_description(w, columns, 1), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_row_description_encodes_several_columns([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const protocol_column_t columns[] = {{"id", PROTOCOL_TYPE_INT4}, {"name", PROTOCOL_TYPE_TEXT}};
    const uint8_t expected[] = {
        'R',  0,    0,    0,    8,    0,    0, 0, 0,                    // AuthenticationOk
        'T',  0,    0,    0,    50,   0,    2,                          // RowDescription, 2 columns
        'i',  'd',  0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 23, 0, 4, // id: int4, size 4
        0xFF, 0xFF, 0xFF, 0xFF, 0,    0,                                // modifier -1, text format
        'n',  'a',  'm',  'e',  0,    0,    0, 0, 0, 0, 0, 0, 0,  0, 25, // name: text
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, // size -1, modifier -1, text
    };

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_row_description(w, columns, 2), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_row_description_allows_zero_columns([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'T', 0, 0, 0, 6, 0, 0};

    assert_int_equal(protocol_put_row_description(w, nullptr, 0), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_row_description_rejects_invalid_arguments([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const protocol_column_t column = {"id", PROTOCOL_TYPE_INT4};
    const protocol_column_t unnamed = {nullptr, PROTOCOL_TYPE_INT4};

    errno = 0;
    assert_int_equal(protocol_put_row_description(nullptr, &column, 1), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(protocol_put_row_description(w, nullptr, 1), -1);
    assert_int_equal(protocol_put_row_description(w, &column, (size_t) INT16_MAX + 1), -1);
    assert_int_equal(protocol_put_row_description(w, &unnamed, 1), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_row_description_rejects_unknown_type([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const protocol_column_t columns[] = {{"id", PROTOCOL_TYPE_INT4}, {"x", 9999}};

    errno = 0;
    assert_int_equal(protocol_put_row_description(w, columns, 2), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_data_row_matches_postgres_bytes([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const protocol_value_t values[] = {{"1", 1}, {"ab", 2}, {nullptr, 0}, {"", 0}};
    const uint8_t expected[] = {
        'D',  0,    0,    0,    25,  0,   4, // type, length, 4 values
        0,    0,    0,    1,    '1',         // "1"
        0,    0,    0,    2,    'a', 'b',    // "ab"
        0xFF, 0xFF, 0xFF, 0xFF,              // NULL: length -1, no bytes
        0,    0,    0,    0,                 // "": length 0, no bytes
    };

    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_data_row(w, values, 4), 0);
    assert_int_equal(wire_writer_size(w), 9 + sizeof(expected));
    assert_memory_equal(wire_writer_data(w) + 9, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_data_row_allows_zero_values([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'D', 0, 0, 0, 6, 0, 0};

    assert_int_equal(protocol_put_data_row(w, nullptr, 0), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_data_row_rejects_invalid_arguments([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const protocol_value_t value = {"x", 1};
    const protocol_value_t too_long = {"x", (size_t) INT32_MAX + 1};

    errno = 0;
    assert_int_equal(protocol_put_data_row(nullptr, &value, 1), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(protocol_put_data_row(w, nullptr, 1), -1);
    assert_int_equal(protocol_put_data_row(w, &value, (size_t) INT16_MAX + 1), -1);
    errno = 0;
    assert_int_equal(protocol_put_data_row(w, &too_long, 1), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_authentication_ok_matches_postgres_bytes),
    cmocka_unit_test(test_ready_for_query_encodes_each_status),
    cmocka_unit_test(test_messages_append_after_existing_content),
    cmocka_unit_test(test_ready_for_query_rejects_unknown_status),
    cmocka_unit_test(test_encoders_reject_null_writer),
    cmocka_unit_test(test_command_complete_matches_postgres_bytes),
    cmocka_unit_test(test_command_complete_length_ignores_earlier_content),
    cmocka_unit_test(test_command_complete_rejects_null_arguments),
    cmocka_unit_test(test_error_response_matches_postgres_bytes),
    cmocka_unit_test(test_error_response_length_ignores_earlier_content),
    cmocka_unit_test(test_error_response_rejects_invalid_arguments),
    cmocka_unit_test(test_row_description_matches_postgres_bytes),
    cmocka_unit_test(test_row_description_encodes_several_columns),
    cmocka_unit_test(test_row_description_allows_zero_columns),
    cmocka_unit_test(test_row_description_rejects_invalid_arguments),
    cmocka_unit_test(test_row_description_rejects_unknown_type),
    cmocka_unit_test(test_data_row_matches_postgres_bytes),
    cmocka_unit_test(test_data_row_allows_zero_values),
    cmocka_unit_test(test_data_row_rejects_invalid_arguments),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
