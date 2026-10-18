/**
 * Tests for startup packet handling
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

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

static void assert_declines_with_n(const uint8_t *packet, size_t size)
{
    wire_writer_t *w = create_writer();
    startup_result_t result = STARTUP_DONE;
    const uint8_t expected[] = {'N'};

    assert_int_equal(startup_handle_packet(packet, size, w, &result), 0);
    assert_int_equal(result, STARTUP_CONTINUE);
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
    startup_result_t result = STARTUP_DONE;
    const uint8_t packet[] = {0x04, 0xD2, 0x16, 0x2E, 0, 0, 0, 7, 0, 0, 0, 9};

    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), 0);
    assert_int_equal(result, STARTUP_CLOSE);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_startup_message_is_accepted_without_authentication([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_result_t result = STARTUP_CONTINUE;
    const uint8_t packet[] = {0, 3, 0, 0, 'u', 's', 'e', 'r', 0, 't', 0, 0};
    const uint8_t expected[] = {'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};

    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), 0);
    assert_int_equal(result, STARTUP_DONE);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_newer_minor_version_is_accepted([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_result_t result = STARTUP_CONTINUE;
    const uint8_t packet[] = {0, 3, 0, 2};

    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), 0);
    assert_int_equal(result, STARTUP_DONE);
    wire_writer_destroy(w);
}

static void test_short_packet_is_rejected([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_result_t result = STARTUP_CONTINUE;
    const uint8_t packet[] = {0, 3};

    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, &result), -1);
    assert_int_equal(errno, EBADMSG);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_null_arguments_are_rejected([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    startup_result_t result = STARTUP_CONTINUE;
    const uint8_t packet[] = {0, 3, 0, 0};

    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), nullptr, &result), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(startup_handle_packet(packet, sizeof(packet), w, nullptr), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(startup_handle_packet(nullptr, sizeof(packet), w, &result), -1);
    assert_int_equal(errno, EINVAL);
    wire_writer_destroy(w);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_ssl_request_is_declined),
    cmocka_unit_test(test_gss_encryption_request_is_declined),
    cmocka_unit_test(test_cancel_request_closes_without_reply),
    cmocka_unit_test(test_startup_message_is_accepted_without_authentication),
    cmocka_unit_test(test_newer_minor_version_is_accepted),
    cmocka_unit_test(test_short_packet_is_rejected),
    cmocka_unit_test(test_null_arguments_are_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
