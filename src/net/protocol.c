/**
 * Encoders for the Postgres backend (server to client) messages.
 */

#include "net/protocol.h"

#include <errno.h>

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
