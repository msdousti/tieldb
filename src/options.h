/**
 * Command-line options of the tieldb server.
 *
 *   --port N   listen on port N (0 picks a free port); default 5432, like Postgres
 *   --once     serve a single connection, then exit
 *   --data-dir PATH   keep the tables in the data directory PATH (see storage/data_dir.h): it is
 *              loaded at startup
 *   --in-memory   keep everything in memory, lost when the server exits
 *
 * Exactly one of --data-dir and --in-memory is required, so that data is never lost by forgetting
 * an option and no default directory is chosen for the user.
 *
 * Errors return -1 and set errno: EINVAL for an unknown option, a missing or malformed port
 * value, a port above 65535, --data-dir without a path, or neither or both of --data-dir and
 * --in-memory.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t port;
    size_t max_connections;
    const char *data_dir; // nullptr without --data-dir; points into argv
    bool in_memory;
} options_t;

[[nodiscard]] int options_parse(int argc, char *const *argv, options_t *options);
