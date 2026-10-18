#define _GNU_SOURCE
/**
 * Integration tests: run the real tieldb binary and talk to it over TCP
 *
 * The path of the binary is the first command-line argument (see tests/meson.build). The server
 * is started with --port 0 and announces the port it got on its first line of output.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>

#include "net/server.h"

static constexpr size_t REPLY_CAPACITY = 1024;
static constexpr size_t ANNOUNCEMENT_CAPACITY = 64;
static constexpr int DECIMAL_BASE = 10;
static constexpr size_t PORT_TEXT_CAPACITY = 8;
static const char ANNOUNCEMENT[] = "listening on port ";
static const char *tieldb_path;

static const uint8_t SESSION[] = {
    0,   0,   0,   8,   0x04, 0xD2, 0x16, 0x2F,                                 // SSLRequest
    0,   0,   0,   9,   0,    3,    0,    0,    0,                              // StartupMessage
    'Q', 0,   0,   0,   22,   's',  'e',  'l',  'e', 'c', 't', ' ',             // Query
    'v', 'e', 'r', 's', 'i',  'o',  'n',  '(',  ')', ';', 0,   'X', 0, 0, 0, 4, // Terminate
};

typedef struct {
    pid_t pid;
    FILE *output;
} child_t;

static child_t start_tieldb(const char *port_argument, const char *extra_argument)
{
    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    pid_t pid = fork();
    assert_true(pid >= 0);
    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execl(tieldb_path, tieldb_path, "--port", port_argument, extra_argument, (char *) nullptr);
        _exit(127);
    }
    close(pipe_fds[1]);
    return (child_t){.pid = pid, .output = fdopen(pipe_fds[0], "r")};
}

static int wait_for_exit_status(child_t child)
{
    int status = 0;
    fclose(child.output);
    assert_int_equal(waitpid(child.pid, &status, 0), child.pid);
    assert_true(WIFEXITED(status));
    return WEXITSTATUS(status);
}

static uint16_t read_announced_port(child_t child)
{
    char line[ANNOUNCEMENT_CAPACITY];
    if (fgets(line, sizeof(line), child.output) == nullptr ||
        strncmp(line, ANNOUNCEMENT, strlen(ANNOUNCEMENT)) != 0) {
        fail();
    }
    return (uint16_t) strtoul(line + strlen(ANNOUNCEMENT), nullptr, DECIMAL_BASE);
}

static int connect_to(uint16_t port)
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fail();
    }
    assert_int_equal(connect(fd, (const struct sockaddr *) &address, sizeof(address)), 0);
    return fd;
}

static size_t read_until_closed(int fd, char *reply)
{
    size_t size = 0;
    ssize_t count = 0;
    while ((count = read(fd, reply + size, REPLY_CAPACITY - size)) > 0) {
        size += (size_t) count;
    }
    return size;
}

static void assert_contains(const char *haystack, size_t size, const char *needle)
{
    if (memmem(haystack, size, needle, strlen(needle)) == nullptr) {
        fail();
    }
}

static void test_serves_a_query_session_then_exits([[maybe_unused]] void **state)
{
    char reply[REPLY_CAPACITY];
    child_t child = start_tieldb("0", "--once");
    int fd = connect_to(read_announced_port(child));

    assert_int_equal(write(fd, SESSION, sizeof(SESSION)), (ssize_t) sizeof(SESSION));
    size_t size = read_until_closed(fd, reply);
    close(fd);

    assert_contains(reply, size, "TielDB 0.1.0");
    assert_int_equal(wait_for_exit_status(child), 0);
}

static void test_survives_a_client_that_disconnects_abruptly([[maybe_unused]] void **state)
{
    child_t child = start_tieldb("0", "--once");
    int fd = connect_to(read_announced_port(child));

    assert_int_equal(write(fd, SESSION, sizeof(SESSION) - 1), (ssize_t) sizeof(SESSION) - 1);
    close(fd);

    assert_int_equal(wait_for_exit_status(child), 0);
}

static void test_rejects_an_unknown_option_with_usage_status([[maybe_unused]] void **state)
{
    child_t child = start_tieldb("0", "--bogus");

    assert_int_equal(wait_for_exit_status(child), 2);
}

static server_t *create_server(void)
{
    server_t *server = server_create(0);
    if (server == nullptr) {
        fail();
    }
    return server;
}

static void test_fails_when_the_port_is_taken([[maybe_unused]] void **state)
{
    server_t *taken = create_server();
    char port[PORT_TEXT_CAPACITY];
    snprintf(port, sizeof(port), "%u", server_port(taken));

    child_t child = start_tieldb(port, "--once");

    assert_int_equal(wait_for_exit_status(child), 1);
    server_destroy(taken);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_serves_a_query_session_then_exits),
    cmocka_unit_test(test_survives_a_client_that_disconnects_abruptly),
    cmocka_unit_test(test_rejects_an_unknown_option_with_usage_status),
    cmocka_unit_test(test_fails_when_the_port_is_taken),
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 1;
    }
    tieldb_path = argv[1];
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
