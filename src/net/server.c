/**
 * TCP listener that serves client connections one after another.
 */

#include "net/server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net/connection.h"

static constexpr int LISTEN_BACKLOG = 16;

struct server {
    int fd;
    uint16_t port;
};

// Each call runs even if an earlier one failed, which is harmless here; errno keeps the first
// failure because successful calls leave it alone
static int configure_listener(int fd, uint16_t requested_port, uint16_t *bound_port)
{
    const int enable = 1;
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(requested_port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    socklen_t length = sizeof(address);

    int status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    status |= bind(fd, (const struct sockaddr *) &address, sizeof(address));
    status |= listen(fd, LISTEN_BACKLOG);
    status |= getsockname(fd, (struct sockaddr *) &address, &length);
    *bound_port = ntohs(address.sin_port);
    return status;
}

static void discard(server_t *server)
{
    int saved_errno = errno;
    close(server->fd);
    free(server);
    errno = saved_errno;
}

server_t *server_create(uint16_t port)
{
    server_t *server = calloc(1, sizeof(*server));
    if (server == nullptr) {
        return nullptr;
    }
    server->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->fd < 0) {
        free(server);
        return nullptr;
    }
    if (configure_listener(server->fd, port, &server->port) != 0) {
        discard(server);
        return nullptr;
    }
    return server;
}

void server_destroy(server_t *server)
{
    close(server->fd);
    free(server);
}

uint16_t server_port(const server_t *server)
{
    return server->port;
}

static int serve_next(const server_t *server)
{
    int client = accept(server->fd, nullptr, nullptr);
    if (client < 0) {
        return -1;
    }
    (void) connection_serve(client);
    close(client);
    return 0;
}

int server_serve(server_t *server, size_t max_connections)
{
    int status = 0;
    for (size_t served = 0; served < max_connections && status == 0; served++) {
        status = serve_next(server);
    }
    return status;
}
