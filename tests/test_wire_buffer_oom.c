/**
 * Out-of-memory tests for the wire protocol output buffer.
 *
 * wire_buffer.c is built for this test with malloc, calloc and realloc renamed to the oom_*
 * functions below (see tests/meson.build). Each one asks cmocka whether the call succeeds, so
 * every test spells out the allocation sequence it expects, and cmocka fails the test if the
 * code makes more or fewer calls.
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmocka.h>

#include "net/connection.h"
#include "net/protocol.h"
#include "net/query_handler.h"
#include "net/startup.h"
#include "net/wire_buffer.h"

// NOLINTBEGIN(misc-use-internal-linkage): called from wire_buffer.c, a separate translation unit
void *oom_malloc(size_t size)
{
    if (mock_type(bool)) {
        return malloc(size);
    }
    errno = ENOMEM;
    return nullptr;
}

void *oom_calloc(size_t count, size_t size)
{
    if (mock_type(bool)) {
        return calloc(count, size);
    }
    errno = ENOMEM;
    return nullptr;
}

void *oom_realloc(void *ptr, size_t size)
{
    if (mock_type(bool)) {
        return realloc(ptr, size);
    }
    errno = ENOMEM;
    return nullptr;
}
// NOLINTEND(misc-use-internal-linkage)

static wire_writer_t *create_writer_with_real_allocations(void)
{
    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    wire_writer_t *writer = wire_writer_create();
    if (writer == nullptr) {
        fail();
    }
    return writer;
}

// Mirrors the writer's initial capacity: the first write that crosses it must grow the buffer
static constexpr size_t WRITER_CAPACITY = 64;

// Bytes to pre-fill so that the write ending at bytes_through_failing_write is the first to
// overflow the capacity, and therefore the one that has to grow (and fails in these tests)
static size_t fill_to_overflow_at(size_t bytes_through_failing_write)
{
    return WRITER_CAPACITY + 1 - bytes_through_failing_write;
}

static constexpr size_t MESSAGE_HEADER_SIZE = sizeof(char) + sizeof(int32_t);
static constexpr size_t FIELD_COUNT_SIZE = sizeof(int16_t);
static constexpr size_t COLUMN_FIXED_SIZE = 18;
static constexpr size_t TERMINATOR_SIZE = 1;
static constexpr size_t AUTHENTICATION_OK_SIZE = sizeof(char) + sizeof(int32_t) + sizeof(int32_t);
static constexpr size_t READY_FOR_QUERY_SIZE = MESSAGE_HEADER_SIZE + sizeof(char);

static size_t cstring_size(const char *str)
{
    return strlen(str) + 1;
}

static size_t error_field_size(const char *value)
{
    return sizeof(char) + cstring_size(value);
}

static wire_writer_t *create_writer_filled_to(size_t size)
{
    uint8_t filler[WRITER_CAPACITY] = {};
    wire_writer_t *writer = create_writer_with_real_allocations();
    if (wire_writer_put_bytes(writer, filler, size) != 0) {
        fail();
    }
    return writer;
}

static void test_create_fails_when_struct_allocation_fails([[maybe_unused]] void **state)
{
    will_return(oom_calloc, false);
    errno = 0;

    wire_writer_t *w = wire_writer_create();

    assert_null(w);
    assert_int_equal(errno, ENOMEM);
}

static void test_create_fails_when_buffer_allocation_fails([[maybe_unused]] void **state)
{
    will_return(oom_calloc, true);
    will_return(oom_malloc, false);
    errno = 0;

    wire_writer_t *w = wire_writer_create();

    assert_null(w);
    assert_int_equal(errno, ENOMEM);
}

static void test_put_bytes_fails_when_growth_fails([[maybe_unused]] void **state)
{
    uint8_t chunk[200] = {};
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(wire_writer_put_bytes(w, chunk, sizeof(chunk)), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_reader_create_fails_when_allocation_fails([[maybe_unused]] void **state)
{
    const uint8_t input[] = {1};
    will_return(oom_calloc, false);
    errno = 0;

    wire_reader_t *r = wire_reader_create(input, sizeof(input));

    assert_null(r);
    assert_int_equal(errno, ENOMEM);
}

static void
test_command_complete_leaves_writer_unchanged_when_header_fails([[maybe_unused]] void **state)
{
    size_t filled = fill_to_overflow_at(MESSAGE_HEADER_SIZE);
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_command_complete_rolls_back_header_when_tag_fails([[maybe_unused]] void **state)
{
    char tag[100];
    memset(tag, 'x', sizeof(tag) - 1);
    tag[sizeof(tag) - 1] = '\0';
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_command_complete(w, tag), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void assert_error_response_fails_leaving(size_t filled)
{
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_error_response(w, "ERROR", "42601", "x"), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_error_response_rolls_back_when_header_fails([[maybe_unused]] void **state)
{
    assert_error_response_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE));
}

static void test_error_response_rolls_back_when_field_code_fails([[maybe_unused]] void **state)
{
    assert_error_response_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE + sizeof(char)));
}

static void test_error_response_rolls_back_when_field_value_fails([[maybe_unused]] void **state)
{
    assert_error_response_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + sizeof(char) + cstring_size("ERROR")));
}

static void test_error_response_rolls_back_when_terminator_fails([[maybe_unused]] void **state)
{
    assert_error_response_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + error_field_size("ERROR") +
                            error_field_size("42601") + error_field_size("x") + TERMINATOR_SIZE));
}

static void assert_row_description_fails_leaving(size_t filled)
{
    const protocol_column_t columns[] = {{"id", PROTOCOL_TYPE_INT4}, {"name", PROTOCOL_TYPE_TEXT}};
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_row_description(w, columns, 2), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_row_description_rolls_back_when_header_fails([[maybe_unused]] void **state)
{
    assert_row_description_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE));
}

static void test_row_description_rolls_back_when_count_fails([[maybe_unused]] void **state)
{
    assert_row_description_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE));
}

static void test_row_description_rolls_back_when_name_fails([[maybe_unused]] void **state)
{
    assert_row_description_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE + cstring_size("id")));
}

static void test_row_description_rolls_back_when_column_fields_fail([[maybe_unused]] void **state)
{
    assert_row_description_fails_leaving(fill_to_overflow_at(
        MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE + cstring_size("id") + COLUMN_FIXED_SIZE));
}

static void assert_data_row_fails_leaving(size_t filled)
{
    const protocol_value_t values[] = {{"ab", 2}, {nullptr, 0}};
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_data_row(w, values, 2), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_data_row_rolls_back_when_header_fails([[maybe_unused]] void **state)
{
    assert_data_row_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE));
}

static void test_data_row_rolls_back_when_count_fails([[maybe_unused]] void **state)
{
    assert_data_row_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE));
}

static void test_data_row_rolls_back_when_value_length_fails([[maybe_unused]] void **state)
{
    assert_data_row_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE + sizeof(int32_t)));
}

static void test_data_row_rolls_back_when_value_bytes_fail([[maybe_unused]] void **state)
{
    assert_data_row_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE +
                                                      sizeof(int32_t) + strlen("ab")));
}

static void test_data_row_rolls_back_finished_value_when_next_fails([[maybe_unused]] void **state)
{
    assert_data_row_fails_leaving(fill_to_overflow_at(
        MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE + sizeof(int32_t) + strlen("ab") + sizeof(int32_t)));
}

static void assert_startup_fails_leaving(size_t filled)
{
    const uint8_t packet[] = {0, 3, 0, 0};
    startup_result_t result = STARTUP_CONTINUE;
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_calloc, true);
    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_startup_leaves_writer_unchanged_when_reader_fails([[maybe_unused]] void **state)
{
    const uint8_t packet[] = {0, 3, 0, 0};
    startup_result_t result = STARTUP_CONTINUE;
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, false);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_startup_rolls_back_when_authentication_ok_fails([[maybe_unused]] void **state)
{
    assert_startup_fails_leaving(fill_to_overflow_at(AUTHENTICATION_OK_SIZE));
}

static void test_startup_rolls_back_when_ready_for_query_fails([[maybe_unused]] void **state)
{
    assert_startup_fails_leaving(
        fill_to_overflow_at(AUTHENTICATION_OK_SIZE + READY_FOR_QUERY_SIZE));
}

static void test_startup_leaves_result_unset_when_decline_write_fails([[maybe_unused]] void **state)
{
    const uint8_t packet[] = {0x04, 0xD2, 0x16, 0x2F};
    startup_result_t result = STARTUP_DONE;
    wire_writer_t *w = create_writer_filled_to(WRITER_CAPACITY);

    will_return(oom_calloc, true);
    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(result, STARTUP_DONE);
    assert_int_equal(wire_writer_size(w), WRITER_CAPACITY);
    wire_writer_destroy(w);
}

static void test_query_response_is_rolled_back_when_a_message_fails([[maybe_unused]] void **state)
{
    const char *sql = "SELECT version()";
    size_t row_description_size =
        MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE + cstring_size("version") + COLUMN_FIXED_SIZE;
    size_t filled = fill_to_overflow_at(row_description_size + MESSAGE_HEADER_SIZE);
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_calloc, true);
    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void
test_query_fails_cleanly_when_the_reader_cannot_be_created([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, false);
    errno = 0;
    assert_int_equal(query_handle_message("SELECT 1", sizeof("SELECT 1"), w), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_connection_fails_when_the_writer_cannot_be_created([[maybe_unused]] void **state)
{
    will_return(oom_calloc, false);

    errno = 0;
    assert_int_equal(connection_serve(-1), -1);
    assert_int_equal(errno, ENOMEM);
}

static void test_connection_fails_when_a_packet_cannot_be_buffered([[maybe_unused]] void **state)
{
    const uint8_t ssl_request[] = {0, 0, 0, 8, 0x04, 0xD2, 0x16, 0x2F};
    int pair[2];
    assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    assert_int_equal(write(pair[1], ssl_request, sizeof(ssl_request)),
                     (ssize_t) sizeof(ssl_request));
    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    will_return(oom_malloc, false);

    errno = 0;
    assert_int_equal(connection_serve(pair[0]), -1);
    assert_int_equal(errno, ENOMEM);
    close(pair[0]);
    close(pair[1]);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_create_fails_when_struct_allocation_fails),
    cmocka_unit_test(test_create_fails_when_buffer_allocation_fails),
    cmocka_unit_test(test_put_bytes_fails_when_growth_fails),
    cmocka_unit_test(test_reader_create_fails_when_allocation_fails),
    cmocka_unit_test(test_command_complete_leaves_writer_unchanged_when_header_fails),
    cmocka_unit_test(test_command_complete_rolls_back_header_when_tag_fails),
    cmocka_unit_test(test_error_response_rolls_back_when_header_fails),
    cmocka_unit_test(test_error_response_rolls_back_when_field_code_fails),
    cmocka_unit_test(test_error_response_rolls_back_when_field_value_fails),
    cmocka_unit_test(test_error_response_rolls_back_when_terminator_fails),
    cmocka_unit_test(test_row_description_rolls_back_when_header_fails),
    cmocka_unit_test(test_row_description_rolls_back_when_count_fails),
    cmocka_unit_test(test_row_description_rolls_back_when_name_fails),
    cmocka_unit_test(test_row_description_rolls_back_when_column_fields_fail),
    cmocka_unit_test(test_data_row_rolls_back_when_header_fails),
    cmocka_unit_test(test_data_row_rolls_back_when_count_fails),
    cmocka_unit_test(test_data_row_rolls_back_when_value_length_fails),
    cmocka_unit_test(test_data_row_rolls_back_when_value_bytes_fail),
    cmocka_unit_test(test_data_row_rolls_back_finished_value_when_next_fails),
    cmocka_unit_test(test_startup_leaves_writer_unchanged_when_reader_fails),
    cmocka_unit_test(test_startup_rolls_back_when_authentication_ok_fails),
    cmocka_unit_test(test_startup_rolls_back_when_ready_for_query_fails),
    cmocka_unit_test(test_startup_leaves_result_unset_when_decline_write_fails),
    cmocka_unit_test(test_query_response_is_rolled_back_when_a_message_fails),
    cmocka_unit_test(test_query_fails_cleanly_when_the_reader_cannot_be_created),
    cmocka_unit_test(test_connection_fails_when_the_writer_cannot_be_created),
    cmocka_unit_test(test_connection_fails_when_a_packet_cannot_be_buffered),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
