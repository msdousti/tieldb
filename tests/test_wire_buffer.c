/**
 * Tests for wire protocol output buffer
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "net/wire_buffer.h"

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

static void assert_failed_with(int result, int expected_errno)
{
    assert_int_equal(result, -1);
    assert_int_equal(errno, expected_errno);
}

static void test_writer_encodes_big_endian([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {0xAB, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    assert_int_equal(wire_writer_put_u8(w, 0xAB), 0);
    assert_int_equal(wire_writer_put_i16(w, 0x0102), 0);
    assert_int_equal(wire_writer_put_i32(w, 0x03040506), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_cstring_includes_terminator([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'u', 's', 'e', 'r', 0, 0};

    assert_int_equal(wire_writer_put_cstring(w, "user"), 0);
    assert_int_equal(wire_writer_put_cstring(w, ""), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_grows_past_initial_capacity([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    uint8_t chunk[1000];
    memset(chunk, 0x5A, sizeof(chunk));

    for (int i = 0; i < 10; i++) {
        assert_int_equal(wire_writer_put_bytes(w, chunk, sizeof(chunk)), 0);
    }
    assert_int_equal(wire_writer_size(w), 10 * sizeof(chunk));
    assert_memory_equal(wire_writer_data(w), chunk, sizeof(chunk));
    wire_writer_destroy(w);
}

static void test_writer_rejects_size_overflow([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    uint8_t byte = 1;

    assert_int_equal(wire_writer_put_u8(w, byte), 0);
    errno = 0;
    assert_failed_with(wire_writer_put_bytes(w, &byte, SIZE_MAX), EOVERFLOW);
    assert_int_equal(wire_writer_size(w), 1);
    wire_writer_destroy(w);
}

static void test_writer_rejects_capacity_overflow([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    uint8_t byte = 1;

    errno = 0;
    assert_failed_with(wire_writer_put_bytes(w, &byte, SIZE_MAX / 2 + 100), EOVERFLOW);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_writer_encodes_negative_values([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {0xFF, 0xFE, 0xFF, 0xFF, 0x80, 0x00};

    assert_int_equal(wire_writer_put_i16(w, -2), 0);
    assert_int_equal(wire_writer_put_i32(w, -32768), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_accepts_empty_bytes([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    assert_int_equal(wire_writer_put_bytes(w, nullptr, 0), 0);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_writer_rejects_null_input([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    errno = 0;
    assert_failed_with(wire_writer_put_bytes(w, nullptr, 1), EINVAL);
    errno = 0;
    assert_failed_with(wire_writer_put_cstring(w, nullptr), EINVAL);
    errno = 0;
    assert_failed_with(wire_writer_put_u8(nullptr, 1), EINVAL);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_writer_encodes_big_endian),
        cmocka_unit_test(test_writer_cstring_includes_terminator),
        cmocka_unit_test(test_writer_grows_past_initial_capacity),
        cmocka_unit_test(test_writer_rejects_size_overflow),
        cmocka_unit_test(test_writer_rejects_capacity_overflow),
        cmocka_unit_test(test_writer_encodes_negative_values),
        cmocka_unit_test(test_writer_accepts_empty_bytes),
        cmocka_unit_test(test_writer_rejects_null_input),
    };
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
