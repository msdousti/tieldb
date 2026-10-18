/**
 * TCP listener that serves client connections one after another.
 */

#include "net/server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net/connection.h"

static constexpr int LISTEN_BACKLOG = 16;

struct server {
    int fd;
    uint16_t port;
    pthread_mutex_t lock;
    pthread_cond_t sessions_ended;
    size_t active_sessions;
};

typedef struct {
    server_t *server;
    int client;
} session_t;

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
    server->lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    server->sessions_ended = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
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
    pthread_cond_destroy(&server->sessions_ended);
    pthread_mutex_destroy(&server->lock);
    free(server);
}

uint16_t server_port(const server_t *server)
{
    return server->port;
}

static void count_session(server_t *server, int change)
{
    pthread_mutex_lock(&server->lock);
    server->active_sessions += (size_t) change;
    pthread_cond_broadcast(&server->sessions_ended);
    pthread_mutex_unlock(&server->lock);
}

static void *run_session(void *argument)
{
    session_t *session = argument;
    server_t *server = session->server;
    (void) connection_serve(session->client);
    close(session->client);
    free(session);
    count_session(server, -1);
    return nullptr;
}

static int start_session(server_t *server, int client)
{
    session_t *session = malloc(sizeof(*session));
    if (session == nullptr) {
        return -1;
    }
    *session = (session_t){.server = server, .client = client};
    count_session(server, 1);
    pthread_t thread;
    int error = pthread_create(&thread, nullptr, run_session, session);
    if (error != 0) {
        free(session);
        count_session(server, -1);
        errno = error;
        return -1;
    }
    pthread_detach(thread);
    return 0;
}

static int serve_next(server_t *server)
{
    int client = accept(server->fd, nullptr, nullptr);
    if (client < 0) {
        return -1;
    }
    if (start_session(server, client) != 0) {
        close(client);
        return -1;
    }
    return 0;
}

static void wait_for_sessions(server_t *server)
{
    pthread_mutex_lock(&server->lock);
    while (server->active_sessions > 0) {
        pthread_cond_wait(&server->sessions_ended, &server->lock);
    }
    pthread_mutex_unlock(&server->lock);
}

int server_serve(server_t *server, size_t max_connections)
{
    int status = 0;
    for (size_t accepted = 0; accepted < max_connections && status == 0; accepted++) {
        status = serve_next(server);
    }
    wait_for_sessions(server);
    return status;
}
