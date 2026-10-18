/**
 * Command-line options of the tieldb server.
 *
 *   --port N   listen on port N (0 picks a free port); default 5432, like Postgres
 *   --once     serve a single connection, then exit
 *
 * Errors return -1 and set errno: EINVAL for an unknown option, a missing or malformed port
 * value, or a port above 65535.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t port;
    size_t max_connections;
} options_t;

[[nodiscard]] int options_parse(int argc, char *const *argv, options_t *options);
