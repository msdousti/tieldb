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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_authentication_ok_matches_postgres_bytes),
        cmocka_unit_test(test_ready_for_query_encodes_each_status),
        cmocka_unit_test(test_messages_append_after_existing_content),
        cmocka_unit_test(test_ready_for_query_rejects_unknown_status),
        cmocka_unit_test(test_encoders_reject_null_writer),
        cmocka_unit_test(test_command_complete_matches_postgres_bytes),
        cmocka_unit_test(test_command_complete_length_ignores_earlier_content),
        cmocka_unit_test(test_command_complete_rejects_null_arguments),
    };
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
