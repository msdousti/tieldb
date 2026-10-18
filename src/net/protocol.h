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
 *   DataRow: one row of values, matching the last RowDescription
 *
 *     0     1  2  3  4    5  6    7 ...
 *    'D'    length        count   one entry per column
 *
 *     each entry is an int32 byte length followed by that many bytes of text
 *     (no terminator); a length of -1 means SQL NULL and no bytes follow
 *
 *   RowDescription: the columns of the rows that follow
 *
 *     0     1  2  3  4    5  6    7 ...
 *    'T'    length        count   one entry per column
 *
 *     each column entry is the name (NUL-terminated) followed by 18 bytes:
 *       table OID (int32, 0), column number (int16, 0), type OID (int32),
 *       type size (int16; -1 for variable length), type modifier (int32, -1),
 *       format code (int16, 0 = text; only text is supported)
 *
 *   ErrorResponse: a command failed; psql prints it as "ERROR:  message"
 *
 *     0     1  2  3  4    5 ...
 *    'E'    length        fields, then a 0 byte
 *
 *     each field is a one-byte code and a NUL-terminated value:
 *       'S' severity, e.g. "ERROR" or "FATAL"
 *       'C' SQLSTATE error code, always 5 characters, e.g. "42601" (syntax error)
 *       'M' human-readable message
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
 * a null writer or an invalid argument gives EINVAL; for RowDescription that
 * includes a type OID other than the PROTOCOL_TYPE_* values, and for DataRow a
 * value longer than INT32_MAX bytes.
 */

#pragma once

#include "net/wire_buffer.h"

/** The transaction state reported in ReadyForQuery. */
typedef enum {
    TRANSACTION_IDLE = 'I',     // not in a transaction block
    TRANSACTION_IN_BLOCK = 'T', // in a transaction block
    TRANSACTION_FAILED = 'E',   // in a failed transaction block; queries are rejected
} transaction_status_t;

/** Postgres type OIDs for the supported column types. */
enum {
    PROTOCOL_TYPE_INT4 = 23,
    PROTOCOL_TYPE_TEXT = 25,
};

/** One result column: its name and Postgres type OID (the type size follows from the OID). */
typedef struct protocol_column {
    const char *name;
    int32_t type_oid;
} protocol_column_t;

/** One value of a DataRow: length bytes of text, or SQL NULL when data is nullptr. */
typedef struct protocol_value {
    const char *data;
    size_t length;
} protocol_value_t;

[[nodiscard]] int protocol_put_authentication_ok(wire_writer_t *writer);
[[nodiscard]] int protocol_put_row_description(wire_writer_t *writer,
                                               const protocol_column_t *columns, size_t count);
[[nodiscard]] int protocol_put_data_row(wire_writer_t *writer, const protocol_value_t *values,
                                        size_t count);
[[nodiscard]] int protocol_put_command_complete(wire_writer_t *writer, const char *tag);
[[nodiscard]] int protocol_put_error_response(wire_writer_t *writer, const char *severity,
                                              const char *sqlstate, const char *message);
[[nodiscard]] int protocol_put_ready_for_query(wire_writer_t *writer, transaction_status_t status);
