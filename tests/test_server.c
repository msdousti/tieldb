/**
 * Tests for the TCP listener
 *
 * Clients run in the test process: connecting only needs the listener's accept queue, so a test
 * connects and sends everything first, then lets server_serve handle the connections.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
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
    0,   0, 0, 8, 0, 3, 0, 0, // StartupMessage
    'X', 0, 0, 0, 4,          // Terminate
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

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_create_reports_the_port_it_listens_on),
    cmocka_unit_test(test_create_fails_when_the_port_is_taken),
    cmocka_unit_test(test_serves_connections_in_turn),
    cmocka_unit_test(test_client_hanging_up_does_not_stop_the_server),
    cmocka_unit_test(test_create_fails_when_no_descriptor_is_free),
    cmocka_unit_test(test_serve_fails_when_a_connection_cannot_be_accepted),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
