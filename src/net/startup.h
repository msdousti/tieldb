/**
 * Handling of the packets a client sends before the normal message flow.
 *
 * Unlike every other frontend message, these have no type byte: the client sends a four-byte
 * length (counting itself), then a packet that starts with a four-byte code:
 *
 *   code                  meaning                      server reply
 *   -------------------   --------------------------   -----------------------------------
 *   80877103              SSLRequest                   'N' (TLS is not supported)
 *   80877104              GSSENCRequest                'N' (not supported)
 *   80877102              CancelRequest                none; the connection is closed
 *   3.x (0x0003xxxx)      StartupMessage               AuthenticationOk, ReadyForQuery
 *
 * After an 'N' the client may send another packet on the same connection, typically the
 * StartupMessage. A StartupMessage also carries parameters such as the user and database name;
 * TielDB has no authentication or databases yet, so it accepts every connection and ignores them.
 *
 * Errors return -1 and set errno: EINVAL for a null argument, EBADMSG for a packet too short to
 * hold a code, EPROTONOSUPPORT for a protocol major version other than 3. On failure the output
 * writer is left unchanged and *result is not meaningful.
 */

#pragma once

#include "net/protocol.h"

/** What the connection should do next. */
typedef enum {
    STARTUP_CONTINUE, // the reply was sent; expect another startup packet
    STARTUP_DONE,     // the connection is established; expect regular messages
    STARTUP_CLOSE,    // close the connection
} startup_result_t;

/**
 * Handles one startup packet (the bytes after the length field) and appends the reply, if any,
 * to out. *result tells the caller how to continue.
 */
[[nodiscard]] int startup_handle_packet(const void *packet, size_t size, wire_writer_t *out,
                                        startup_result_t *result);
