/**
 * Serves one client connection from the startup packets to the Terminate message.
 */

#include "net/connection.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "io/io.h"
#include "net/protocol.h"
#include "net/query_handler.h"
#include "net/startup.h"

static constexpr uint8_t MESSAGE_QUERY = 'Q';
static constexpr uint8_t MESSAGE_TERMINATE = 'X';
static constexpr size_t LENGTH_SIZE = sizeof(int32_t);
static constexpr char ERROR_SEVERITY[] = "ERROR";
static constexpr char UNSUPPORTED_SQLSTATE[] = "0A000";
static constexpr char UNSUPPORTED_MESSAGE[] = "unsupported message type";

typedef struct {
    uint8_t type;
    uint8_t *body;
    size_t size;
} frame_t;

static ssize_t read_some(int fd, void *buffer, size_t size)
{
    ssize_t count = io->read(fd, buffer, size);
    if (count == 0) {
        errno = ECONNRESET;
        return -1;
    }
    return count;
}

static int read_exact(int fd, void *buffer, size_t size)
{
    uint8_t *bytes = buffer;
    size_t done = 0;
    ssize_t count = 1;
    while (done < size && count > 0) {
        count = read_some(fd, bytes + done, size - done);
        done += (size_t) (count > 0 ? count : 0);
    }
    return done == size ? 0 : -1;
}

static int write_all(int fd, const uint8_t *data, size_t size)
{
    size_t done = 0;
    ssize_t count = 1;
    while (done < size && count > 0) {
        count = io->write(fd, data + done, size - done);
        done += (size_t) (count > 0 ? count : 0);
    }
    return done == size ? 0 : -1;
}

static int flush_replies(int fd, wire_writer_t *out)
{
    int status = write_all(fd, wire_writer_data(out), wire_writer_size(out));
    wire_writer_truncate(out, 0);
    return status;
}

static size_t decode_body_size(const uint8_t raw[static LENGTH_SIZE])
{
    uint32_t length = 0;
    for (size_t i = 0; i < LENGTH_SIZE; i++) {
        length = (length << CHAR_BIT) | raw[i];
    }
    return length - LENGTH_SIZE;
}

static int read_frame(int fd, bool has_type, frame_t *frame)
{
    uint8_t raw[LENGTH_SIZE];
    if (has_type && read_exact(fd, &frame->type, sizeof(frame->type)) != 0) {
        return -1;
    }
    if (read_exact(fd, raw, sizeof(raw)) != 0) {
        return -1;
    }
    frame->size = decode_body_size(raw);
    if (frame->size == 0) {
        return 0;
    }
    frame->body = malloc(frame->size);
    if (frame->body == nullptr) {
        return -1;
    }
    return read_exact(fd, frame->body, frame->size);
}

static int put_unsupported_message(wire_writer_t *out)
{
    // The error text always fits in the empty writer, so only ReadyForQuery's status is checked
    (void) protocol_put_error_response(out, ERROR_SEVERITY, UNSUPPORTED_SQLSTATE,
                                       UNSUPPORTED_MESSAGE);
    return protocol_put_ready_for_query(out, TRANSACTION_IDLE);
}

static int answer_message(const frame_t *frame, wire_writer_t *out)
{
    if (frame->type == MESSAGE_QUERY) {
        return query_handle_message(frame->body, frame->size, out);
    }
    return put_unsupported_message(out);
}

static int exchange_startup(int fd, wire_writer_t *out, startup_result_t *result)
{
    frame_t frame = {};
    int status = read_frame(fd, false, &frame);
    if (status == 0) {
        status = startup_handle_packet(frame.body, frame.size, out, result);
    }
    free(frame.body);
    return status == 0 ? flush_replies(fd, out) : status;
}

static int run_startup(int fd, wire_writer_t *out, startup_result_t *result)
{
    int status = 0;
    do {
        status = exchange_startup(fd, out, result);
    } while (status == 0 && *result == STARTUP_CONTINUE);
    return status;
}

static int exchange_message(int fd, wire_writer_t *out, bool *terminated)
{
    frame_t frame = {};
    int status = read_frame(fd, true, &frame);
    *terminated = status == 0 && frame.type == MESSAGE_TERMINATE;
    if (status == 0 && !*terminated) {
        status = answer_message(&frame, out);
    }
    free(frame.body);
    if (status == 0 && !*terminated) {
        status = flush_replies(fd, out);
    }
    return status;
}

static int run_messages(int fd, wire_writer_t *out)
{
    bool terminated = false;
    int status = 0;
    while (status == 0 && !terminated) {
        status = exchange_message(fd, out, &terminated);
    }
    return status;
}

int connection_serve(int fd)
{
    wire_writer_t *out = wire_writer_create();
    if (out == nullptr) {
        return -1;
    }
    startup_result_t result = STARTUP_CONTINUE;
    int status = run_startup(fd, out, &result);
    if (status == 0 && result == STARTUP_DONE) {
        status = run_messages(fd, out);
    }
    wire_writer_destroy(out);
    return status;
}
