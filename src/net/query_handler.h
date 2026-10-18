/**
 * Handler for the simple Query message ('Q'), hardcoded for now.
 *
 * The body of a Query message is one NUL-terminated SQL string. Three single-value queries are
 * answered with RowDescription, one DataRow, CommandComplete and ReadyForQuery:
 *
 *   SELECT version()           the server version
 *   SELECT current_user        the user name from the session
 *   SELECT current_database()  the database name from the session
 *
 * The other statements are in executor.h. The match ignores case, surrounding whitespace and one
 * trailing semicolon, so what psql sends for "select version();" works. Any other text gets an
 * ErrorResponse (SQLSTATE 0A000, feature not supported) followed by ReadyForQuery, so the
 * session stays usable.
 *
 * Either response is appended in full, or not at all. Errors return -1 and set errno: EINVAL for
 * a null writer or session, EBADMSG for a body without a string, ENOMEM when memory runs out.
 */

#pragma once

#include "net/protocol.h"
#include "net/session.h"

[[nodiscard]] int query_handle_message(const void *body, size_t size, wire_writer_t *out,
                                       const session_info_t *session);
