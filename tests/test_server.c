/**
 * Tests for the TCP listener
 *
 * Clients run in the test process: connecting only needs the listener's accept queue, so a test
 * connects and sends everything first, then lets server_serve handle the connections.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmocka.h>

#include "net/server.h"

static const uint8_t SESSION[] = {
    0,   0, 0, 9, 0, 3, 0, 0, 0, // StartupMessage
    'X', 0, 0, 0, 4,             // Terminate
};

static server_t *create_server(void)
{
    server_t *server = server_create(0);
    if (server == nullptr) {
        fail();
    }
    return server;
}

static int connect_client(const server_t *server)
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port(server)),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fail();
    }
    assert_int_equal(connect(fd, (const struct sockaddr *) &address, sizeof(address)), 0);
    return fd;
}

static void send_session(int fd)
{
    assert_int_equal(write(fd, SESSION, sizeof(SESSION)), (ssize_t) sizeof(SESSION));
}

static void assert_server_said_authentication_ok(int fd)
{
    uint8_t reply[64];
    ssize_t size = read(fd, reply, sizeof(reply));
    assert_true(size > 0);
    assert_int_equal(reply[0], 'R');
}

static void test_create_reports_the_port_it_listens_on([[maybe_unused]] void **state)
{
    server_t *server = create_server();

    assert_true(server_port(server) != 0);
    server_destroy(server);
}

static void test_create_fails_when_the_port_is_taken([[maybe_unused]] void **state)
{
    server_t *first = create_server();

    errno = 0;
    assert_null(server_create(server_port(first)));
    assert_int_equal(errno, EADDRINUSE);
    server_destroy(first);
}

static void test_serves_connections_in_turn([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    int first = connect_client(server);
    int second = connect_client(server);
    send_session(first);
    send_session(second);

    assert_int_equal(server_serve(server, 2), 0);

    assert_server_said_authentication_ok(first);
    assert_server_said_authentication_ok(second);
    close(first);
    close(second);
    server_destroy(server);
}

static void test_client_hanging_up_does_not_stop_the_server([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    int hung_up = connect_client(server);
    int patient = connect_client(server);
    close(hung_up);
    send_session(patient);

    assert_int_equal(server_serve(server, 2), 0);

    assert_server_said_authentication_ok(patient);
    close(patient);
    server_destroy(server);
}

static void limit_descriptors_to_the_next_free_one(struct rlimit *saved)
{
    int next_free = dup(STDIN_FILENO);
    assert_true(next_free >= 0);
    close(next_free);
    struct rlimit limit = {.rlim_cur = (rlim_t) next_free, .rlim_max = RLIM_INFINITY};
    assert_int_equal(getrlimit(RLIMIT_NOFILE, saved), 0);
    limit.rlim_max = saved->rlim_max;
    assert_int_equal(setrlimit(RLIMIT_NOFILE, &limit), 0);
}

static void test_create_fails_when_no_descriptor_is_free([[maybe_unused]] void **state)
{
    struct rlimit saved;
    limit_descriptors_to_the_next_free_one(&saved);

    errno = 0;
    server_t *server = server_create(0);
    int error = errno;

    assert_int_equal(setrlimit(RLIMIT_NOFILE, &saved), 0);
    assert_null(server);
    assert_int_equal(error, EMFILE);
}

static void test_serve_fails_when_a_connection_cannot_be_accepted([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    int client = connect_client(server);
    struct rlimit saved;
    limit_descriptors_to_the_next_free_one(&saved);

    errno = 0;
    int status = server_serve(server, 2);
    int error = errno;

    assert_int_equal(setrlimit(RLIMIT_NOFILE, &saved), 0);
    assert_int_equal(status, -1);
    assert_int_equal(error, EMFILE);
    close(client);
    server_destroy(server);
}

typedef struct {
    server_t *server;
    int status;
} serving_t;

static void *serve_two_connections(void *argument)
{
    serving_t *serving = argument;
    serving->status = server_serve(serving->server, 2);
    return nullptr;
}

static void test_a_stuck_client_does_not_block_others([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    pthread_t thread;
    serving_t serving = {.server = server, .status = -1};
    int stuck = connect_client(server);
    int active = connect_client(server);

    assert_int_equal(pthread_create(&thread, nullptr, serve_two_connections, &serving), 0);
    send_session(active);
    assert_server_said_authentication_ok(active);
    send_session(stuck);
    assert_server_said_authentication_ok(stuck);

    assert_int_equal(pthread_join(thread, nullptr), 0);
    assert_int_equal(serving.status, 0);
    close(stuck);
    close(active);
    server_destroy(server);
}

static void *return_immediately(void *argument)
{
    return argument;
}

// The limit does not apply to root or to processes with CAP_SYS_RESOURCE, so check that it works
static bool thread_creation_is_blocked(void)
{
    pthread_t thread;
    if (pthread_create(&thread, nullptr, return_immediately, nullptr) != 0) {
        return true;
    }
    assert_int_equal(pthread_join(thread, nullptr), 0);
    return false;
}

static bool block_thread_creation(struct rlimit *saved)
{
    assert_int_equal(getrlimit(RLIMIT_NPROC, saved), 0);
    struct rlimit none = {.rlim_cur = 0, .rlim_max = saved->rlim_max};
    assert_int_equal(setrlimit(RLIMIT_NPROC, &none), 0);
    if (thread_creation_is_blocked()) {
        return true;
    }
    assert_int_equal(setrlimit(RLIMIT_NPROC, saved), 0);
    return false;
}

static void test_serve_fails_when_a_thread_cannot_be_started([[maybe_unused]] void **state)
{
    struct rlimit saved;
    if (!block_thread_creation(&saved)) {
        skip();
    }
    server_t *server = create_server();
    int client = connect_client(server);

    errno = 0;
    int status = server_serve(server, 1);
    int error = errno;

    assert_int_equal(setrlimit(RLIMIT_NPROC, &saved), 0);
    assert_int_equal(status, -1);
    assert_int_equal(error, EAGAIN);
    close(client);
    server_destroy(server);
}

typedef struct {
    server_t *server;
    size_t max_connections;
    int result;
} serve_job_t;

static void *serve_in_a_thread(void *argument)
{
    serve_job_t *job = argument;
    job->result = server_serve(job->server, job->max_connections);
    return nullptr;
}

static int connect_and_wait_for_the_session(const server_t *server)
{
    int fd = connect_client(server);

    assert_int_equal(write(fd, SESSION, 9), 9);
    assert_server_said_authentication_ok(fd);
    return fd;
}

// Reads what the server still has to say, and then the end of the connection
static void assert_hung_up_on(int fd)
{
    uint8_t buffer[256];
    ssize_t count = 0;

    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {}
    assert_int_equal(count, 0);
}

static void test_stop_ends_the_sessions_and_serve_returns_zero([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    serve_job_t job = {server, SIZE_MAX, -1};
    pthread_t thread;
    int first = -1;
    int second = -1;

    assert_int_equal(pthread_create(&thread, nullptr, serve_in_a_thread, &job), 0);
    first = connect_and_wait_for_the_session(server);
    second = connect_and_wait_for_the_session(server);
    server_stop(server);
    server_stop(server);
    assert_int_equal(pthread_join(thread, nullptr), 0);

    assert_int_equal(job.result, 0);
    assert_hung_up_on(first);
    assert_hung_up_on(second);
    close(first);
    close(second);
    server_destroy(server);
}

static void test_stop_during_the_wait_for_the_last_session([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    serve_job_t job = {server, 1, -1};
    pthread_t thread;
    int client = -1;

    assert_int_equal(pthread_create(&thread, nullptr, serve_in_a_thread, &job), 0);
    client = connect_and_wait_for_the_session(server);
    server_stop(server);
    assert_int_equal(pthread_join(thread, nullptr), 0);

    assert_int_equal(job.result, 0);
    assert_hung_up_on(client);
    close(client);
    server_destroy(server);
}

static void test_stop_before_serving_ends_serve_at_once([[maybe_unused]] void **state)
{
    server_t *server = create_server();

    server_stop(server);

    assert_int_equal(server_serve(server, SIZE_MAX), 0);
    server_destroy(server);
}

static void leave_with_terminate(int client)
{
    static const uint8_t TERMINATE[] = {'X', 0, 0, 0, 4};

    assert_int_equal(write(client, TERMINATE, sizeof(TERMINATE)), 5);
    assert_hung_up_on(client);
    close(client);
}

static void test_sessions_may_end_in_any_order([[maybe_unused]] void **state)
{
    server_t *server = create_server();
    serve_job_t job = {server, SIZE_MAX, -1};
    pthread_t thread;
    int clients[3];

    assert_int_equal(pthread_create(&thread, nullptr, serve_in_a_thread, &job), 0);
    for (size_t i = 0; i < 3; i++) {
        clients[i] = connect_and_wait_for_the_session(server);
    }
    leave_with_terminate(clients[1]); // in the middle of the list of sessions
    leave_with_terminate(clients[2]); // the newest
    leave_with_terminate(clients[0]); // the last one
    server_stop(server);
    assert_int_equal(pthread_join(thread, nullptr), 0);
    assert_int_equal(job.result, 0);
    server_destroy(server);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_create_reports_the_port_it_listens_on),
    cmocka_unit_test(test_create_fails_when_the_port_is_taken),
    cmocka_unit_test(test_serves_connections_in_turn),
    cmocka_unit_test(test_stop_ends_the_sessions_and_serve_returns_zero),
    cmocka_unit_test(test_stop_during_the_wait_for_the_last_session),
    cmocka_unit_test(test_stop_before_serving_ends_serve_at_once),
    cmocka_unit_test(test_sessions_may_end_in_any_order),
    cmocka_unit_test(test_client_hanging_up_does_not_stop_the_server),
    cmocka_unit_test(test_a_stuck_client_does_not_block_others),
    cmocka_unit_test(test_serve_fails_when_a_thread_cannot_be_started),
    cmocka_unit_test(test_create_fails_when_no_descriptor_is_free),
    cmocka_unit_test(test_serve_fails_when_a_connection_cannot_be_accepted),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
