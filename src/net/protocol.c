/**
 * Encoders for the Postgres backend (server to client) messages.
 */

#include "net/protocol.h"

#include <errno.h>

static int begin_message(wire_writer_t *writer, uint8_t type)
{
    const uint8_t header[] = {type, 0, 0, 0, 0};
    return wire_writer_put_bytes(writer, header, sizeof(header));
}

static int end_message(wire_writer_t *writer, size_t start)
{
    size_t length = wire_writer_size(writer) - start - 1;
    return wire_writer_patch_i32(writer, start + 1, (int32_t) length);
}

int protocol_put_authentication_ok(wire_writer_t *writer)
{
    static const uint8_t message[] = {'R', 0, 0, 0, 8, 0, 0, 0, 0};
    return wire_writer_put_bytes(writer, message, sizeof(message));
}

int protocol_put_ready_for_query(wire_writer_t *writer, transaction_status_t status)
{
    if (status != TRANSACTION_IDLE && status != TRANSACTION_IN_BLOCK &&
        status != TRANSACTION_FAILED) {
        errno = EINVAL;
        return -1;
    }
    const uint8_t message[] = {'Z', 0, 0, 0, 5, (uint8_t) status};
    return wire_writer_put_bytes(writer, message, sizeof(message));
}

int protocol_put_command_complete(wire_writer_t *writer, const char *tag)
{
    if (writer == nullptr || tag == nullptr) {
        errno = EINVAL;
        return -1;
    }
    size_t start = wire_writer_size(writer);
    if (begin_message(writer, 'C') != 0 || wire_writer_put_cstring(writer, tag) != 0) {
        wire_writer_truncate(writer, start);
        return -1;
    }
    return end_message(writer, start);
}
