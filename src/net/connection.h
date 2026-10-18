/**
 * Serves one client connection from the startup packets to the Terminate message.
 *
 * The connection is a blocking file descriptor, read and written through the injectable io
 * interface. It runs the startup exchange (startup.h), then answers frontend messages until the
 * client sends Terminate ('X'):
 *
 *   type   meaning        reply
 *   ----   ------------   ----------------------------------------------------------
 *   'Q'    Query          query_handle_message
 *   'X'    Terminate      none; the connection ends
 *   other  unsupported    ErrorResponse (SQLSTATE 0A000), then ReadyForQuery
 *
 * Replies are sent after each message, so the session stays usable after an error.
 *
 * Returns 0 when the session ended cleanly (Terminate, or a CancelRequest, which is answered by
 * closing). Returns -1 and sets errno otherwise: ECONNRESET when the client closed the
 * connection mid-session, ENOMEM when memory runs out, or the error of a failed read or write.
 * The descriptor is not closed; that is the caller's job.
 */

#pragma once

[[nodiscard]] int connection_serve(int fd);
