/**
 * The tieldb server: listens for Postgres clients and serves each on its own thread.
 *
 * SIGTERM and SIGINT stop it cleanly: it stops accepting, lets the sessions finish the statement
 * they are running, saves the data directory (if it has one) and exits with status 0.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "net/server.h"
#include "options.h"
#include "storage/data_dir.h"

static constexpr int EXIT_USAGE = 2;
static constexpr char USAGE[] =
    "usage: tieldb [--port N] [--once] (--data-dir PATH | --in-memory)\n";

static int open_data_dir(const char *dir)
{
    if (dir == nullptr) {
        return 0;
    }
    if (data_dir_init(dir) != 0 || data_dir_load(dir) != 0) {
        perror("tieldb: data directory");
        return -1;
    }
    return 0;
}

static void fill_stop_signals(sigset_t *signals)
{
    sigemptyset(signals);
    sigaddset(signals, SIGTERM);
    sigaddset(signals, SIGINT);
}

static void *wait_for_stop_signal(void *argument)
{
    sigset_t signals;
    int received = 0;

    fill_stop_signals(&signals);
    sigwait(&signals, &received);
    server_stop(argument);
    return nullptr;
}

// SIGTERM and SIGINT are blocked in every thread, so that only this one takes them, and no
// handler has to run in the middle of whatever a thread is doing
static int watch_stop_signals(server_t *server, pthread_t *watcher)
{
    int error = pthread_create(watcher, nullptr, wait_for_stop_signal, server);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}

// The watcher uses the server, so it must be over before the server is destroyed: if it is still
// waiting (the server ended by itself) a signal of our own wakes it, and in any case it is joined
static void stop_watching(pthread_t watcher)
{
    // SIGINT is blocked everywhere but in the watcher, so this ends nothing but its wait; the call
    // fails only for a watcher that is over, which is fine
    (void) pthread_kill(watcher, SIGINT);
    (void) pthread_join(watcher, nullptr);
}

static int save_data_dir(const char *dir)
{
    if (dir == nullptr) {
        return 0;
    }
    if (data_dir_checkpoint(dir) != 0) {
        perror("tieldb: saving the data directory");
        return -1;
    }
    return 0;
}

static server_t *start_server(uint16_t port, pthread_t *watcher)
{
    server_t *server = server_create(port);
    if (server == nullptr) {
        perror("tieldb");
        return nullptr;
    }
    if (watch_stop_signals(server, watcher) != 0) {
        perror("tieldb: watching for SIGTERM");
        server_destroy(server);
        return nullptr;
    }
    return server;
}

static int serve(const options_t *options)
{
    sigset_t signals;
    fill_stop_signals(&signals);
    (void) pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    // A client that disconnects mid-reply must produce a write error, not kill the server
    signal(SIGPIPE, SIG_IGN);
    pthread_t watcher;
    server_t *server = start_server(options->port, &watcher);
    if (server == nullptr) {
        return EXIT_FAILURE;
    }
    printf("listening on port %u\n", server_port(server));
    fflush(stdout);
    int status = server_serve(server, options->max_connections);
    stop_watching(watcher);
    int saved = save_data_dir(options->data_dir);
    server_destroy(server);
    return (status | saved) != 0; // either failed
}

int main(int argc, char **argv)
{
    options_t options;
    if (options_parse(argc, argv, &options) != 0) {
        fputs(USAGE, stderr);
        return EXIT_USAGE;
    }
    if (open_data_dir(options.data_dir) != 0) {
        return EXIT_FAILURE;
    }
    return serve(&options);
}
