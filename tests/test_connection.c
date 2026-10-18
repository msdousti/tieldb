/**
 * Tests for the per-connection message loop
 *
 * A fake client is one end of a socketpair: the test writes everything the client would send,
 * runs connection_serve on the other end, then reads everything the server sent back.
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmocka.h>

#include "io/io.h"
#include "net/connection.h"
#include "net/protocol.h"

static constexpr size_t TRANSCRIPT_CAPACITY = 1024;

static const uint8_t SSL_REQUEST[] = {0, 0, 0, 8, 0x04, 0xD2, 0x16, 0x2F};
static const uint8_t STARTUP_MESSAGE[] = {0, 0, 0, 8, 0, 3, 0, 0};
static const uint8_t CANCEL_REQUEST[] = {0, 0, 0, 16, 0x04, 0xD2, 0x16, 0x2E,
                                         0, 0, 0, 7,  0,    0,    0,    9};
static const uint8_t VERSION_QUERY[] = {'Q', 0,   0,   0,   22,  's', 'e', 'l', 'e', 'c', 't', ' ',
                                        'v', 'e', 'r', 's', 'i', 'o', 'n', '(', ')', ';', 0};
static const uint8_t UNKNOWN_MESSAGE[] = {'P', 0, 0, 0, 4};
static const uint8_t TERMINATE[] = {'X', 0, 0, 0, 4};

typedef struct {
    int result;
    int error;
    uint8_t reply[TRANSCRIPT_CAPACITY];
    size_t reply_size;
} session_t;

static void drain_peer(int peer, session_t *session)
{
    ssize_t count = 0;
    while ((count = read(peer, session->reply + session->reply_size,
                         sizeof(session->reply) - session->reply_size)) > 0) {
        session->reply_size += (size_t) count;
    }
}

static void run_session(const uint8_t *input, size_t size, session_t *session)
{
    int pair[2];
    assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    assert_int_equal(write(pair[1], input, size), (ssize_t) size);
    assert_int_equal(shutdown(pair[1], SHUT_WR), 0);

    errno = 0;
    session->result = connection_serve(pair[0]);
    session->error = errno;
    close(pair[0]);
    session->reply_size = 0;
    drain_peer(pair[1], session);
    close(pair[1]);
}

static size_t append(uint8_t *to, size_t used, const uint8_t *from, size_t size)
{
    memcpy(to + used, from, size);
    return used + size;
}

static void assert_reply_is(const session_t *session, const wire_writer_t *expected)
{
    assert_int_equal(session->reply_size, wire_writer_size(expected));
    assert_memory_equal(session->reply, wire_writer_data(expected), session->reply_size);
}

static wire_writer_t *expected_greeting(void)
{
    wire_writer_t *w = wire_writer_create();
    if (w == nullptr) {
        fail();
    }
    assert_int_equal(wire_writer_put_u8(w, 'N'), 0);
    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
    return w;
}

static void put_version_reply(wire_writer_t *w)
{
    const protocol_column_t column = {"version", PROTOCOL_TYPE_TEXT};
    const protocol_value_t value = {"TielDB 0.1.0", 12};
    assert_int_equal(protocol_put_row_description(w, &column, 1), 0);
    assert_int_equal(protocol_put_data_row(w, &value, 1), 0);
    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static size_t build_input(uint8_t *input, const uint8_t *tail, size_t tail_size)
{
    size_t used = append(input, 0, SSL_REQUEST, sizeof(SSL_REQUEST));
    used = append(input, used, STARTUP_MESSAGE, sizeof(STARTUP_MESSAGE));
    return append(input, used, tail, tail_size);
}

static void test_full_session_answers_query_until_terminate([[maybe_unused]] void **state)
{
    uint8_t input[TRANSCRIPT_CAPACITY];
    session_t session;
    uint8_t tail[sizeof(VERSION_QUERY) + sizeof(TERMINATE)];
    append(tail, append(tail, 0, VERSION_QUERY, sizeof(VERSION_QUERY)), TERMINATE,
           sizeof(TERMINATE));

    size_t size = build_input(input, tail, sizeof(tail));
    run_session(input, size, &session);

    wire_writer_t *expected = expected_greeting();
    put_version_reply(expected);
    assert_int_equal(session.result, 0);
    assert_reply_is(&session, expected);
    wire_writer_destroy(expected);
}

static void test_unsupported_message_gets_error_and_session_continues([[maybe_unused]] void **state)
{
    uint8_t input[TRANSCRIPT_CAPACITY];
    session_t session;
    uint8_t tail[sizeof(UNKNOWN_MESSAGE) + sizeof(TERMINATE)];
    append(tail, append(tail, 0, UNKNOWN_MESSAGE, sizeof(UNKNOWN_MESSAGE)), TERMINATE,
           sizeof(TERMINATE));

    size_t size = build_input(input, tail, sizeof(tail));
    run_session(input, size, &session);

    wire_writer_t *expected = expected_greeting();
    assert_int_equal(
        protocol_put_error_response(expected, "ERROR", "0A000", "unsupported message type"), 0);
    assert_int_equal(protocol_put_ready_for_query(expected, TRANSACTION_IDLE), 0);
    assert_int_equal(session.result, 0);
    assert_reply_is(&session, expected);
    wire_writer_destroy(expected);
}

static void test_cancel_request_ends_session_without_reply([[maybe_unused]] void **state)
{
    session_t session;

    run_session(CANCEL_REQUEST, sizeof(CANCEL_REQUEST), &session);

    assert_int_equal(session.result, 0);
    assert_int_equal(session.reply_size, 0);
}

static void test_client_hanging_up_mid_session_is_connection_reset([[maybe_unused]] void **state)
{
    uint8_t input[TRANSCRIPT_CAPACITY];
    session_t session;

    size_t size = build_input(input, TERMINATE, 0);
    run_session(input, size, &session);

    assert_int_equal(session.result, -1);
    assert_int_equal(session.error, ECONNRESET);
}

static ssize_t one_byte_read(int fd, void *buffer, size_t count)
{
    return io_real_ops.read(fd, buffer, count > 0 ? 1 : 0);
}

static ssize_t one_byte_write(int fd, const void *data, size_t count)
{
    return io_real_ops.write(fd, data, count > 0 ? 1 : 0);
}

static ssize_t failing_read([[maybe_unused]] int fd, [[maybe_unused]] void *buffer,
                            [[maybe_unused]] size_t count)
{
    errno = EIO;
    return -1;
}

static ssize_t third_write_fails(int fd, const void *data, size_t count)
{
    static int calls = 0;
    if (++calls == 3) {
        errno = EPIPE;
        return -1;
    }
    return io_real_ops.write(fd, data, count);
}

static void test_read_error_is_reported([[maybe_unused]] void **state)
{
    static const io_ops_t ops = {.read = failing_read, .write = one_byte_write};
    const io_ops_t *saved = io;
    session_t session;

    io = &ops;
    run_session(SSL_REQUEST, sizeof(SSL_REQUEST), &session);
    io = saved;

    assert_int_equal(session.result, -1);
    assert_int_equal(session.error, EIO);
}

static void test_write_error_after_a_message_is_reported([[maybe_unused]] void **state)
{
    static const io_ops_t ops = {.read = one_byte_read, .write = third_write_fails};
    const io_ops_t *saved = io;
    uint8_t input[TRANSCRIPT_CAPACITY];
    session_t session;

    size_t size = build_input(input, UNKNOWN_MESSAGE, sizeof(UNKNOWN_MESSAGE));
    io = &ops;
    run_session(input, size, &session);
    io = saved;

    assert_int_equal(session.result, -1);
    assert_int_equal(session.error, EPIPE);
}

static void test_partial_reads_and_writes_are_completed([[maybe_unused]] void **state)
{
    static const io_ops_t ops = {.read = one_byte_read, .write = one_byte_write};
    const io_ops_t *saved = io;
    uint8_t input[TRANSCRIPT_CAPACITY];
    session_t session;

    size_t size = build_input(input, TERMINATE, sizeof(TERMINATE));
    io = &ops;
    run_session(input, size, &session);
    io = saved;

    wire_writer_t *expected = expected_greeting();
    assert_int_equal(session.result, 0);
    assert_reply_is(&session, expected);
    wire_writer_destroy(expected);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_full_session_answers_query_until_terminate),
    cmocka_unit_test(test_unsupported_message_gets_error_and_session_continues),
    cmocka_unit_test(test_cancel_request_ends_session_without_reply),
    cmocka_unit_test(test_client_hanging_up_mid_session_is_connection_reset),
    cmocka_unit_test(test_read_error_is_reported),
    cmocka_unit_test(test_write_error_after_a_message_is_reported),
    cmocka_unit_test(test_partial_reads_and_writes_are_completed),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
