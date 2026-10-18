/**
 * TCP listener that serves client connections one after another.
 *
 * A server listens on the loopback interface. server_serve accepts a connection, runs it to
 * completion with connection_serve, closes it, and moves on to the next. There is no
 * concurrency yet: a second client waits in the kernel's accept queue until the first is done.
 *
 * A client that disconnects abruptly ends only its own session; the server keeps accepting.
 *
 * Errors return nullptr or -1 and set errno: ENOMEM when memory runs out, otherwise the error of
 * the failed socket call (for example EADDRINUSE when the port is taken).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct server server_t;

/** Starts listening on the port. Port 0 picks a free one; server_port reports it. */
[[nodiscard]] server_t *server_create(uint16_t port);
void server_destroy(server_t *server);
[[nodiscard]] uint16_t server_port(const server_t *server);

/** Serves up to max_connections clients in turn. Returns -1 if accepting a connection fails. */
[[nodiscard]] int server_serve(server_t *server, size_t max_connections);
