/**
 * Encoders for the Postgres backend (server to client) messages.
 *
 * See wire_buffer.h for the general message layout: a type byte, a four-byte
 * big-endian length that counts itself but not the type byte, then the body.
 *
 *   AuthenticationOk: the server accepted the connection (no password needed)
 *
 *     0     1  2  3  4    5  6  7  8
 *    'R'    length = 8    result = 0
 *
 *   CommandComplete: a command finished; the tag names it, e.g. "SELECT 5"
 *
 *     0     1  2  3  4    5 ...
 *    'C'    length        tag, then a NUL byte
 *
 *   ReadyForQuery: the server is idle and waiting for the next query
 *
 *     0     1  2  3  4    5
 *    'Z'    length = 5    status
 *
 * Each encoder appends one complete message to a wire_writer_t, or nothing on
 * failure. Failures return -1 and set errno as documented in wire_buffer.h;
 * a null writer or an invalid argument gives EINVAL.
 */

#pragma once

#include "net/wire_buffer.h"

/** The transaction state reported in ReadyForQuery. */
typedef enum {
    TRANSACTION_IDLE = 'I',     // not in a transaction block
    TRANSACTION_IN_BLOCK = 'T', // in a transaction block
    TRANSACTION_FAILED = 'E',   // in a failed transaction block; queries are rejected
} transaction_status_t;

[[nodiscard]] int protocol_put_authentication_ok(wire_writer_t *writer);
[[nodiscard]] int protocol_put_command_complete(wire_writer_t *writer, const char *tag);
[[nodiscard]] int protocol_put_ready_for_query(wire_writer_t *writer, transaction_status_t status);
