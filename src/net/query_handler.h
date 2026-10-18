/**
 * Handler for the simple Query message ('Q'), hardcoded for now.
 *
 * The body of a Query message is one NUL-terminated SQL string. Until the parser and executor
 * exist, only one statement is answered:
 *
 *   SELECT version()   ->  RowDescription, one DataRow, CommandComplete, ReadyForQuery
 *
 * The match ignores case, surrounding whitespace and one trailing semicolon, so what psql sends
 * for "select version();" works. Any other text gets an ErrorResponse (SQLSTATE 0A000, feature
 * not supported) followed by ReadyForQuery, so the session stays usable.
 *
 * Either response is appended in full, or not at all. Errors return -1 and set errno: EINVAL for
 * a null writer, EBADMSG for a body without a string, ENOMEM when memory runs out.
 */

#pragma once

#include "net/protocol.h"

[[nodiscard]] int query_handle_message(const void *body, size_t size, wire_writer_t *out);
