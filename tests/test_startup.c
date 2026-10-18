/**
 * Tests for startup packet handling
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "net/startup.h"

static wire_writer_t *create_writer(void)
{
    wire_writer_t *writer = wire_writer_create();
    if (writer == nullptr) {
        fail();
    }
    return writer;
}

static void assert_writer_holds(const wire_writer_t *writer, const uint8_t *expected, size_t size)
{
    assert_int_equal(wire_writer_size(writer), size);
    assert_memory_equal(wire_writer_data(writer), expected, size);
}

static void put_expected_acceptance(wire_writer_t *w)
{
    assert_int_equal(protocol_put_authentication_ok(w), 0);
    assert_int_equal(protocol_put_parameter_status(w, "server_version", "18.0 (TielDB 0.1.0)"), 0);
    assert_int_equal(protocol_put_parameter_status(w, "client_encoding", "UTF8"), 0);
    assert_int_equal(protocol_put_ready_for_query(w, TRANSACTION_IDLE), 0);
}

static void assert_declines_with_n(const uint8_t *packet, size_t size)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_DONE};
    const uint8_t expected[] = {'N'};

    assert_int_equal(startup_handle_packet(packet, size, w, &outcome), 0);
    assert_int_equal(outcome.result, STARTUP_CONTINUE);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_ssl_request_is_declined([[maybe_unused]] void **state)
{
    const uint8_t packet[] = {0x04, 0xD2, 0x16, 0x2F};

    assert_declines_with_n(packet, sizeof(packet));
}

static void test_gss_encryption_request_is_declined([[maybe_unused]] void **state)
{
    const uint8_t packet[] = {0x04, 0xD2, 0x16, 0x30};

    assert_declines_with_n(packet, sizeof(packet));
}

static void test_cancel_request_closes_without_reply([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_DONE};
    const uint8_t packet[] = {0x04, 0xD2, 0x16, 0x2E, 0, 0, 0, 7, 0, 0, 0, 9};

    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), 0);
    assert_int_equal(outcome.result, STARTUP_CLOSE);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_startup_message_is_accepted_without_authentication([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};
    const uint8_t packet[] = {0, 3, 0, 0, 'u', 's', 'e', 'r', 0, 't', 0, 0};
    wire_writer_t *expected = create_writer();
    put_expected_acceptance(expected);

    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), 0);
    assert_int_equal(outcome.result, STARTUP_DONE);
    assert_writer_holds(w, wire_writer_data(expected), wire_writer_size(expected));
    wire_writer_destroy(expected);
    wire_writer_destroy(w);
}

static void test_newer_minor_version_is_accepted([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};
    const uint8_t packet[] = {0, 3, 0, 2, 0};

    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), 0);
    assert_int_equal(outcome.result, STARTUP_DONE);
    wire_writer_destroy(w);
}

static void accept_startup(const char *packet, size_t size, startup_outcome_t *outcome)
{
    wire_writer_t *w = create_writer();

    assert_int_equal(startup_handle_packet(packet, size, w, outcome), 0);
    assert_int_equal(outcome->result, STARTUP_DONE);
    wire_writer_destroy(w);
}

static void test_user_and_database_are_taken_from_the_parameters([[maybe_unused]] void **state)
{
    const char packet[] = "\x00\x03\x00\x00"
                          "application_name\0psql\0user\0tester\0database\0testdb\0";
    startup_outcome_t outcome = {};

    accept_startup(packet, sizeof(packet), &outcome);

    assert_string_equal(outcome.session.user, "tester");
    assert_string_equal(outcome.session.database, "testdb");
}

static void test_database_defaults_to_the_user_name([[maybe_unused]] void **state)
{
    const char packet[] = "\x00\x03\x00\x00"
                          "user\0tester\0";
    startup_outcome_t outcome = {};

    accept_startup(packet, sizeof(packet), &outcome);

    assert_string_equal(outcome.session.user, "tester");
    assert_string_equal(outcome.session.database, "tester");
}

static void test_long_names_are_cut_to_fit([[maybe_unused]] void **state)
{
    char packet[256] = "\x00\x03\x00\x00"
                       "user\0";
    startup_outcome_t outcome = {};
    size_t used = 9;

    memset(packet + used, 'a', 100);
    used += 100;
    packet[used++] = '\0';
    packet[used++] = '\0';
    accept_startup(packet, used, &outcome);

    assert_int_equal(strlen(outcome.session.user), SESSION_NAME_CAPACITY - 1);
}

static void assert_startup_rejected(const char *packet, size_t size)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};

    errno = 0;
    assert_int_equal(startup_handle_packet(packet, size, w, &outcome), -1);
    assert_int_equal(errno, EBADMSG);
    assert_int_equal(wire_writer_size(w), 0);
    assert_int_equal(outcome.result, STARTUP_CONTINUE);
    wire_writer_destroy(w);
}

static void test_cut_short_parameters_are_rejected([[maybe_unused]] void **state)
{
    const char no_name_end[] = "\x00\x03\x00\x00"
                               "user";
    const char no_value_end[] = "\x00\x03\x00\x00"
                                "user\0tester";
    const char no_list_end[] = "\x00\x03\x00\x00"
                               "user\0tester\0";

    assert_startup_rejected(no_name_end, sizeof(no_name_end) - 1);
    assert_startup_rejected(no_value_end, sizeof(no_value_end) - 1);
    assert_startup_rejected(no_list_end, sizeof(no_list_end) - 1);
}

static void test_short_packet_is_rejected([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};
    const uint8_t packet[] = {0, 3};

    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &outcome), -1);
    assert_int_equal(errno, EBADMSG);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_null_arguments_are_rejected([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_outcome_t outcome = {.result = STARTUP_CONTINUE};
    const uint8_t packet[] = {0, 3, 0, 0, 0};

    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), nullptr, &outcome), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, nullptr), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(startup_handle_packet(nullptr, sizeof(packet), w, &outcome), -1);
    assert_int_equal(errno, EINVAL);
    wire_writer_destroy(w);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_ssl_request_is_declined),
    cmocka_unit_test(test_gss_encryption_request_is_declined),
    cmocka_unit_test(test_cancel_request_closes_without_reply),
    cmocka_unit_test(test_startup_message_is_accepted_without_authentication),
    cmocka_unit_test(test_newer_minor_version_is_accepted),
    cmocka_unit_test(test_user_and_database_are_taken_from_the_parameters),
    cmocka_unit_test(test_database_defaults_to_the_user_name),
    cmocka_unit_test(test_long_names_are_cut_to_fit),
    cmocka_unit_test(test_cut_short_parameters_are_rejected),
    cmocka_unit_test(test_short_packet_is_rejected),
    cmocka_unit_test(test_null_arguments_are_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
