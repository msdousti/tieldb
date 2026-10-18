/**
 * Handling of the packets a client sends before the normal message flow.
 *
 * Unlike every other frontend message, these have no type byte: the client sends a four-byte
 * length (counting itself), then a packet that starts with a four-byte code:
 *
 *   code                  meaning                      server reply
 *   -------------------   --------------------------   -----------------------------------
 *   80877103 (1234.5679)  SSLRequest                   'N' (TLS is not supported)
 *   80877104 (1234.5680)  GSSENCRequest                'N' (not supported)
 *   80877102 (1234.5678)  CancelRequest                none; the connection is closed
 *   anything else         StartupMessage (3.x)         AuthenticationOk, ParameterStatus x2,
 *                                                      ReadyForQuery
 *
 * The ParameterStatus messages report server_version and client_encoding. psql warns about a
 * version mismatch when it does not get server_version.
 *
 * After an 'N' the client may send another packet on the same connection, typically the
 * StartupMessage. A StartupMessage also carries parameters as pairs of NUL-terminated strings,
 * ending with an empty name. The "user" and "database" ones go into the outcome's session (the
 * database defaults to the user name, as in Postgres); the others are ignored. TielDB has no
 * authentication, so it accepts every connection.
 *
 * The client is assumed to be psql or a well-behaved driver, so nothing here defends against
 * malformed packets: sizes and protocol versions are not validated.
 *
 * Errors return -1 and set errno: EINVAL for a null argument, EBADMSG for a packet too short to
 * hold a code or a StartupMessage whose parameters are cut short. On failure the output writer
 * and *outcome are left unchanged.
 */

#pragma once

#include "net/protocol.h"
#include "net/session.h"

/** What the connection should do next. */
typedef enum {
    STARTUP_CONTINUE, // the reply was sent; expect another startup packet
    STARTUP_DONE,     // the connection is established; expect regular messages
    STARTUP_CLOSE,    // close the connection
} startup_result_t;

/**
 * What a startup packet led to: how to continue, and, once the connection is established
 * (STARTUP_DONE), who the client is.
 */
typedef struct {
    startup_result_t result;
    session_info_t session;
} startup_outcome_t;

/**
 * Handles one startup packet (the bytes after the length field) and appends the reply, if any,
 * to out. outcome->result tells the caller how to continue.
 */
[[nodiscard]] int startup_handle_packet(const void *packet, size_t size, wire_writer_t *out,
                                        startup_outcome_t *outcome);
