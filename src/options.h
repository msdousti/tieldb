/**
 * Command-line options of the tieldb server.
 *
 *   --port N   listen on port N (0 picks a free port); default 5432, like Postgres
 *   --once     serve a single connection, then exit
 *   --data-dir PATH   keep the tables in the data directory PATH (see storage/data_dir.h): it is
 *              loaded at startup. Without it everything is in memory and lost on exit
 *
 * Errors return -1 and set errno: EINVAL for an unknown option, a missing or malformed port
 * value, a port above 65535, or --data-dir without a path.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t port;
    size_t max_connections;
    const char *data_dir; // nullptr without --data-dir; points into argv
} options_t;

[[nodiscard]] int options_parse(int argc, char *const *argv, options_t *options);
