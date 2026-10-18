/**
 * The tieldb server: listens for Postgres clients and serves them one at a time.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "net/server.h"
#include "options.h"

static constexpr int EXIT_USAGE = 2;
static constexpr char USAGE[] = "usage: tieldb [--port N] [--once]\n";

int main(int argc, char **argv)
{
    options_t options;
    if (options_parse(argc, argv, &options) != 0) {
        fputs(USAGE, stderr);
        return EXIT_USAGE;
    }
    // A client that disconnects mid-reply must produce a write error, not kill the server
    signal(SIGPIPE, SIG_IGN);
    server_t *server = server_create(options.port);
    if (server == nullptr) {
        perror("tieldb");
        return EXIT_FAILURE;
    }
    printf("listening on port %u\n", server_port(server));
    fflush(stdout);
    int status = server_serve(server, options.max_connections);
    server_destroy(server);
    return status != 0;
}
