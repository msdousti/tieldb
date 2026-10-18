/**
 * TCP listener that serves each client connection on its own thread.
 *
 * A server listens on the loopback interface. server_serve accepts connections, runs each one
 * with connection_serve on a new thread, and closes it when its session ends. Sessions are
 * independent: a slow or stuck client does not hold up the others. There is no limit on the
 * number of concurrent sessions yet.
 *
 * A client that disconnects abruptly ends only its own session; the server keeps accepting.
 *
 * Errors return nullptr or -1 and set errno: ENOMEM when memory runs out, otherwise the error of
 * the failed socket or thread call (for example EADDRINUSE when the port is taken).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct server server_t;

/** Starts listening on the port. Port 0 picks a free one; server_port reports it. */
[[nodiscard]] server_t *server_create(uint16_t port);
void server_destroy(server_t *server);
[[nodiscard]] uint16_t server_port(const server_t *server);

/**
 * Accepts up to max_connections clients, then waits for all their sessions to end. Returns -1 if
 * accepting a connection or starting its thread fails; sessions already started still finish.
 */
[[nodiscard]] int server_serve(server_t *server, size_t max_connections);
