/**
 * Out-of-memory tests for the wire protocol output buffer.
 *
 * wire_buffer.c is built for this test with malloc, calloc and realloc renamed to the oom_*
 * functions below (see tests/meson.build). Each one asks cmocka whether the call succeeds, so
 * every test spells out the allocation sequence it expects, and cmocka fails the test if the
 * code makes more or fewer calls.
 */

#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmocka.h>

#include "ds/hash_map.h"
#include "net/connection.h"
#include "net/protocol.h"
#include "net/query_handler.h"
#include "net/server.h"
#include "storage/catalog.h"
#include "table_assert.h"
#include "storage/one_page_engine.h"
#include "storage/page_file.h"
#include "net/startup.h"
#include "net/wire_buffer.h"
#include "types/type_registry.h"

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

static const session_info_t SESSION = {.user = "tester", .database = "testdb"};

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

// The writer doubles its capacity when it grows, so a writer grown once holds twice as much
static constexpr size_t GROWN_WRITER_CAPACITY = 2 * WRITER_CAPACITY;

static size_t fill_grown_writer_to_overflow_at(size_t bytes_through_failing_write)
{
    return GROWN_WRITER_CAPACITY + 1 - bytes_through_failing_write;
}

static wire_writer_t *create_grown_writer_filled_to(size_t size)
{
    wire_writer_t *writer = create_writer_filled_to(WRITER_CAPACITY);

    will_return(oom_realloc, true);
    if (wire_writer_put_u8(writer, 0) != 0) {
        fail();
    }
    wire_writer_truncate(writer, size);
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
    const protocol_column_t columns[] = {{"id", TYPE_OID_INT4}, {"name", TYPE_OID_TEXT}};
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

static void assert_startup_fails_leaving(wire_writer_t *w)
{
    const uint8_t packet[] = {0, 3, 0, 0, 0};
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};
    size_t filled = wire_writer_size(w);

    will_return(oom_calloc, true);
    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_startup_leaves_writer_unchanged_when_reader_fails([[maybe_unused]] void **state)
{
    const uint8_t packet[] = {0, 3, 0, 0, 0};
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, false);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static size_t parameter_status_size(const char *name, const char *value)
{
    return MESSAGE_HEADER_SIZE + cstring_size(name) + cstring_size(value);
}

static size_t startup_version_size(void)
{
    return parameter_status_size("server_version", "18.0 (TielDB 0.1.0)");
}

static size_t startup_encoding_size(void)
{
    return parameter_status_size("client_encoding", "UTF8");
}

static void test_startup_rolls_back_when_authentication_ok_fails([[maybe_unused]] void **state)
{
    assert_startup_fails_leaving(
        create_writer_filled_to(fill_to_overflow_at(AUTHENTICATION_OK_SIZE)));
}

static void
test_startup_rolls_back_when_the_first_parameter_status_fails([[maybe_unused]] void **state)
{
    size_t through_version = AUTHENTICATION_OK_SIZE + startup_version_size();

    assert_startup_fails_leaving(create_writer_filled_to(fill_to_overflow_at(through_version)));
}

static void
test_startup_rolls_back_when_the_second_parameter_status_fails([[maybe_unused]] void **state)
{
    size_t through_header = AUTHENTICATION_OK_SIZE + startup_version_size() + MESSAGE_HEADER_SIZE;

    assert_startup_fails_leaving(create_writer_filled_to(fill_to_overflow_at(through_header)));
}

static void test_startup_rolls_back_when_ready_for_query_fails([[maybe_unused]] void **state)
{
    size_t through_ready = AUTHENTICATION_OK_SIZE + startup_version_size() +
                           startup_encoding_size() + READY_FOR_QUERY_SIZE;

    assert_startup_fails_leaving(
        create_grown_writer_filled_to(fill_grown_writer_to_overflow_at(through_ready)));
}

static void assert_parameter_status_fails_leaving(size_t filled)
{
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_parameter_status(w, "name", "value"), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void test_parameter_status_rolls_back_when_header_fails([[maybe_unused]] void **state)
{
    assert_parameter_status_fails_leaving(fill_to_overflow_at(MESSAGE_HEADER_SIZE));
}

static void test_parameter_status_rolls_back_when_name_fails([[maybe_unused]] void **state)
{
    assert_parameter_status_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + cstring_size("name")));
}

static void test_parameter_status_rolls_back_when_value_fails([[maybe_unused]] void **state)
{
    assert_parameter_status_fails_leaving(
        fill_to_overflow_at(MESSAGE_HEADER_SIZE + cstring_size("name") + cstring_size("value")));
}

static void test_startup_leaves_result_unset_when_decline_write_fails([[maybe_unused]] void **state)
{
    const uint8_t packet[] = {0x04, 0xD2, 0x16, 0x2F};
    startup_outcome_t outcome = {.result = STARTUP_DONE};
    wire_writer_t *w = create_writer_filled_to(WRITER_CAPACITY);

    will_return(oom_calloc, true);
    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(outcome.result, STARTUP_DONE);
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
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), filled);
    wire_writer_destroy(w);
}

static void
test_table_response_is_rolled_back_when_a_row_cannot_be_formatted([[maybe_unused]] void **state)
{
    const char *sql = "SELECT * FROM user";
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    will_return(oom_malloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void
test_table_response_is_rolled_back_when_the_cursor_cannot_be_opened([[maybe_unused]] void **state)
{
    const char *sql = "SELECT * FROM user";
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, true);
    will_return(oom_malloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

// Making the user table takes the catalog's hash map (its struct and its buckets), the table, room
// for its cells, and a copy of each of the 6 cells of its 3 rows
static void expect_user_table_creation(void)
{
    will_return(oom_calloc, true);
    will_return(oom_calloc, true);
    will_return(oom_calloc, true);
    for (size_t i = 0; i < 4; i++) {
        will_return(oom_malloc, true);
    }
}

static void test_table_response_is_rolled_back_when_a_row_fails([[maybe_unused]] void **state)
{
    const char *sql = "SELECT * FROM user";
    size_t row_description_size = MESSAGE_HEADER_SIZE + FIELD_COUNT_SIZE + cstring_size("id") +
                                  cstring_size("name") + (2 * COLUMN_FIXED_SIZE);
    size_t filled = fill_to_overflow_at(row_description_size + MESSAGE_HEADER_SIZE);
    wire_writer_t *w = create_writer_filled_to(filled);

    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    will_return(oom_malloc, true);
    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

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
    assert_int_equal(query_handle_message("SELECT 1", sizeof("SELECT 1"), w, &SESSION), -1);

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

static void test_server_create_fails_when_memory_runs_out([[maybe_unused]] void **state)
{
    will_return(oom_calloc, false);

    errno = 0;
    assert_null(server_create(0));
    assert_int_equal(errno, ENOMEM);
}

static void
test_server_serve_fails_when_a_session_cannot_be_allocated([[maybe_unused]] void **state)
{
    will_return(oom_calloc, true);
    server_t *server = server_create(0);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        fail();
    }
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port(server)),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    assert_int_equal(connect(client, (const struct sockaddr *) &address, sizeof(address)), 0);
    will_return(oom_malloc, false);

    errno = 0;
    assert_int_equal(server_serve(server, 1), -1);
    assert_int_equal(errno, ENOMEM);
    close(client);
    server_destroy(server);
}

static const table_column_t PAIR_COLUMNS[] = {{"a", &TYPE_INT4}, {"b", &TYPE_TEXT}};

static table_t *create_pair_table_with_real_allocations(void)
{
    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    table_t *table = table_create("pair", PAIR_COLUMNS, 2);
    if (table == nullptr) {
        fail();
    }
    return table;
}

static void test_one_page_engine_create_fails_when_memory_runs_out([[maybe_unused]] void **state)
{
    const row_layout_t none = {nullptr, 0};

    will_return(oom_malloc, false);
    errno = 0;
    assert_null(one_page_engine_create(&none));
    assert_int_equal(errno, ENOMEM);
}

static void test_one_page_engine_cursor_fails_when_memory_runs_out([[maybe_unused]] void **state)
{
    const row_layout_t none = {nullptr, 0};

    will_return(oom_malloc, true);
    storage_engine_t *engine = one_page_engine_create(&none);
    will_return(oom_malloc, false);
    errno = 0;
    assert_null(storage_open_cursor(engine));
    assert_int_equal(errno, ENOMEM);
    storage_destroy(engine);
}

static void
test_page_file_open_fails_when_the_handle_cannot_be_allocated([[maybe_unused]] void **state)
{
    will_return(oom_malloc, false);

    errno = 0;
    assert_null(page_file_open("/tmp/tieldb_page_file_oom"));
    assert_int_equal(errno, ENOMEM);
}

static void test_table_create_fails_when_memory_runs_out([[maybe_unused]] void **state)
{
    will_return(oom_calloc, false);

    errno = 0;
    assert_null(table_create("pair", PAIR_COLUMNS, 2));
    assert_int_equal(errno, ENOMEM);
}

static void test_table_insert_fails_when_the_row_cannot_be_converted([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table_with_real_allocations();
    const char *row[] = {"1", "x"};

    will_return(oom_malloc, false);
    errno = 0;
    assert_int_equal(table_insert(table, row, 2), -1);

    assert_int_equal(errno, ENOMEM);
    will_return(oom_malloc, true);
    assert_int_equal(count_rows(table), 0);
    table_destroy(table);
}

static void
test_table_create_fails_when_the_engine_cannot_be_allocated([[maybe_unused]] void **state)
{
    will_return(oom_calloc, true);
    will_return(oom_malloc, false);

    errno = 0;
    assert_null(table_create("pair", PAIR_COLUMNS, 2));
    assert_int_equal(errno, ENOMEM);
}

static void
test_insert_fails_when_the_statement_text_cannot_be_copied([[maybe_unused]] void **state)
{
    const char *sql = "INSERT INTO user VALUES (9, 'x')";
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, true);
    will_return(oom_malloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_insert_fails_when_the_row_cannot_be_stored([[maybe_unused]] void **state)
{
    const char *sql = "INSERT INTO user VALUES (9, 'x')";
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    will_return(oom_malloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    will_return(oom_malloc, true);
    assert_int_equal(count_rows(catalog_find("user")), 3);
    wire_writer_destroy(w);
}

static void
test_catalog_create_fails_when_the_table_cannot_be_allocated([[maybe_unused]] void **state)
{
    const table_column_t columns[] = {{"a", &TYPE_INT4}};

    will_return(oom_calloc, false);

    errno = 0;
    assert_null(catalog_create_table("nomemory", columns, 1));
    assert_int_equal(errno, ENOMEM);
}

static void test_create_table_fails_when_memory_runs_out([[maybe_unused]] void **state)
{
    const char *sql = "CREATE TABLE no_memory (a int)";
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    will_return(oom_calloc, false);
    errno = 0;
    assert_int_equal(query_handle_message(sql, strlen(sql) + 1, w, &SESSION), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    assert_null(catalog_find("no_memory"));
    wire_writer_destroy(w);
}

typedef struct {
    hash_node_t node;
    uint32_t key;
} oom_entry_t;

static size_t oom_hash_key(const void *key)
{
    return hash_bytes(key, sizeof(uint32_t));
}

static bool oom_has_key(const hash_node_t *node, const void *key)
{
    const oom_entry_t *entry = HASH_MAP_ENTRY(node, const oom_entry_t, node);
    return entry->key == *(const uint32_t *) key;
}

static const hash_map_ops_t OOM_MAP_OPS = {.hash = oom_hash_key, .has_key = oom_has_key};

static void test_hash_map_create_fails_when_memory_runs_out([[maybe_unused]] void **state)
{
    will_return(oom_calloc, false);
    errno = 0;
    assert_null(hash_map_create(&OOM_MAP_OPS, 8));
    assert_int_equal(errno, ENOMEM);

    will_return(oom_calloc, true);
    will_return(oom_calloc, false);
    errno = 0;
    assert_null(hash_map_create(&OOM_MAP_OPS, 8));
    assert_int_equal(errno, ENOMEM);
}

static void test_hash_map_keeps_working_when_it_cannot_grow([[maybe_unused]] void **state)
{
    oom_entry_t entries[7];
    will_return(oom_calloc, true);
    will_return(oom_calloc, true);
    hash_map_t *map = hash_map_create(&OOM_MAP_OPS, 8);
    if (map == nullptr) {
        fail();
    }

    will_return(oom_calloc, false);
    for (uint32_t i = 0; i < 7; i++) {
        entries[i].key = i;
        hash_map_insert(map, &entries[i].node, &entries[i].key);
    }

    for (uint32_t i = 0; i < 7; i++) {
        assert_ptr_equal(hash_map_find(map, &i), &entries[i].node);
    }
    hash_map_destroy(map);
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
    cmocka_unit_test(test_startup_rolls_back_when_the_first_parameter_status_fails),
    cmocka_unit_test(test_startup_rolls_back_when_the_second_parameter_status_fails),
    cmocka_unit_test(test_startup_rolls_back_when_ready_for_query_fails),
    cmocka_unit_test(test_parameter_status_rolls_back_when_header_fails),
    cmocka_unit_test(test_parameter_status_rolls_back_when_name_fails),
    cmocka_unit_test(test_parameter_status_rolls_back_when_value_fails),
    cmocka_unit_test(test_startup_leaves_result_unset_when_decline_write_fails),
    cmocka_unit_test(test_query_response_is_rolled_back_when_a_message_fails),
    cmocka_unit_test(test_table_response_is_rolled_back_when_a_row_fails),
    cmocka_unit_test(test_table_response_is_rolled_back_when_a_row_cannot_be_formatted),
    cmocka_unit_test(test_table_response_is_rolled_back_when_the_cursor_cannot_be_opened),
    cmocka_unit_test(test_insert_fails_when_the_statement_text_cannot_be_copied),
    cmocka_unit_test(test_insert_fails_when_the_row_cannot_be_stored),
    cmocka_unit_test(test_create_table_fails_when_memory_runs_out),
    cmocka_unit_test(test_query_fails_cleanly_when_the_reader_cannot_be_created),
    cmocka_unit_test(test_connection_fails_when_the_writer_cannot_be_created),
    cmocka_unit_test(test_connection_fails_when_a_packet_cannot_be_buffered),
    cmocka_unit_test(test_server_create_fails_when_memory_runs_out),
    cmocka_unit_test(test_hash_map_create_fails_when_memory_runs_out),
    cmocka_unit_test(test_hash_map_keeps_working_when_it_cannot_grow),
    cmocka_unit_test(test_one_page_engine_create_fails_when_memory_runs_out),
    cmocka_unit_test(test_one_page_engine_cursor_fails_when_memory_runs_out),
    cmocka_unit_test(test_page_file_open_fails_when_the_handle_cannot_be_allocated),
    cmocka_unit_test(test_table_create_fails_when_memory_runs_out),
    cmocka_unit_test(test_catalog_create_fails_when_the_table_cannot_be_allocated),
    cmocka_unit_test(test_table_insert_fails_when_the_row_cannot_be_converted),
    cmocka_unit_test(test_table_create_fails_when_the_engine_cannot_be_allocated),
    cmocka_unit_test(test_server_serve_fails_when_a_session_cannot_be_allocated),
};

// The catalog makes its hash map with the first table; it must report failing to
static int catalog_create_fails_without_memory(const table_column_t *columns)
{
    int status = 0;

    will_return(oom_calloc, false);
    errno = 0;
    status |= catalog_create_table("user", columns, 2) != nullptr || errno != ENOMEM;
    will_return(oom_calloc, true);
    will_return(oom_calloc, false);
    errno = 0;
    status |= catalog_create_table("user", columns, 2) != nullptr || errno != ENOMEM;
    return status;
}

// The tests need the user table, so make it once, with real allocations
static int create_user_table([[maybe_unused]] void **state)
{
    const table_column_t columns[] = {{"id", &TYPE_INT4}, {"name", &TYPE_TEXT}};
    const char *rows[][2] = {{"1", "Alice"}, {"2", "Bob"}, {"3", "Carol"}};
    int status = 0;

    status = catalog_create_fails_without_memory(columns);
    if (status != 0) {
        return status;
    }
    expect_user_table_creation();
    table_t *table = catalog_create_table("user", columns, 2);
    status = table == nullptr;
    for (size_t i = 0; i < 3 && status == 0; i++) {
        status = table_insert(table, rows[i], 2);
    }
    return status;
}

int main(void)
{
    return cmocka_run_group_tests(tests, create_user_table, nullptr);
}
