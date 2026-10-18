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

static void test_writer_encodes_big_endian(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);

    assert_int_equal(wire_writer_put_u8(w, 0xAB), 0);
    assert_int_equal(wire_writer_put_i16(w, 0x0102), 0);
    assert_int_equal(wire_writer_put_i32(w, 0x03040506), 0);

    const uint8_t expected[] = {0xAB, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    assert_int_equal(wire_writer_size(w), sizeof(expected));
    assert_memory_equal(wire_writer_data(w), expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_cstring_includes_terminator(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);

    assert_int_equal(wire_writer_put_cstring(w, "user"), 0);
    assert_int_equal(wire_writer_put_cstring(w, ""), 0);

    const uint8_t expected[] = {'u', 's', 'e', 'r', 0, 0};
    assert_int_equal(wire_writer_size(w), sizeof(expected));
    assert_memory_equal(wire_writer_data(w), expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_grows_past_initial_capacity(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);
    uint8_t chunk[1000];
    memset(chunk, 0x5A, sizeof(chunk));

    for (int i = 0; i < 10; i++) {
        assert_int_equal(wire_writer_put_bytes(w, chunk, sizeof(chunk)), 0);
    }

    assert_int_equal(wire_writer_size(w), 10 * sizeof(chunk));
    assert_memory_equal(wire_writer_data(w), chunk, sizeof(chunk));
    wire_writer_destroy(w);
}

static void test_writer_rejects_size_overflow(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);
    uint8_t byte = 1;

    assert_int_equal(wire_writer_put_u8(w, byte), 0);
    errno = 0;
    assert_int_equal(wire_writer_put_bytes(w, &byte, SIZE_MAX), -1);
    assert_int_equal(errno, EOVERFLOW);

    assert_int_equal(wire_writer_size(w), 1);
    wire_writer_destroy(w);
}

static void test_writer_encodes_negative_values(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);

    assert_int_equal(wire_writer_put_i16(w, -2), 0);
    assert_int_equal(wire_writer_put_i32(w, -32768), 0);

    const uint8_t expected[] = {0xFF, 0xFE, 0xFF, 0xFF, 0x80, 0x00};
    assert_int_equal(wire_writer_size(w), sizeof(expected));
    assert_memory_equal(wire_writer_data(w), expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_accepts_empty_bytes(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);

    assert_int_equal(wire_writer_put_bytes(w, nullptr, 0), 0);

    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_writer_rejects_null_input(void **state)
{
    (void) state;
    wire_writer_t *w = wire_writer_create();
    assert_non_null(w);

    errno = 0;
    assert_int_equal(wire_writer_put_bytes(w, nullptr, 1), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(wire_writer_put_cstring(w, nullptr), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(wire_writer_put_u8(nullptr, 1), -1);
    assert_int_equal(errno, EINVAL);

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
        cmocka_unit_test(test_writer_encodes_negative_values),
        cmocka_unit_test(test_writer_accepts_empty_bytes),
        cmocka_unit_test(test_writer_rejects_null_input),
    };
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
