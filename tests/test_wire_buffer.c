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

static void test_writer_rejects_size_over_protocol_limit([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    uint8_t byte = 1;

    assert_int_equal(wire_writer_put_u8(w, byte), 0);
    errno = 0;
    assert_failed_with(wire_writer_put_bytes(w, &byte, INT32_MAX), EOVERFLOW);
    errno = 0;
    assert_failed_with(wire_writer_put_bytes(w, &byte, SIZE_MAX), EOVERFLOW);
    assert_int_equal(wire_writer_size(w), 1);
    wire_writer_destroy(w);
}

static void test_writer_patches_bytes_in_place([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {0, 0, 0x01, 0x02, 0x03, 0x04, 0};

    assert_int_equal(wire_writer_put_bytes(w, "\0\0\0\0\0\0\0", 7), 0);
    assert_int_equal(wire_writer_patch_i32(w, 2, 0x01020304), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    assert_int_equal(wire_writer_patch_i32(w, 3, -2), 0);
    assert_int_equal(wire_writer_data(w)[3], 0xFF);
    wire_writer_destroy(w);
}

static void test_writer_patch_rejects_out_of_range([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();

    errno = 0;
    assert_failed_with(wire_writer_patch_i32(nullptr, 0, 1), EINVAL);
    errno = 0;
    assert_failed_with(wire_writer_patch_i32(w, 0, 1), EINVAL);
    assert_int_equal(wire_writer_put_i32(w, 7), 0);
    errno = 0;
    assert_failed_with(wire_writer_patch_i32(w, 1, 1), EINVAL);
    assert_int_equal(wire_writer_patch_i32(w, 0, 1), 0);
    wire_writer_destroy(w);
}

static void test_writer_back_patches_message_length([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {'C', 0, 0, 0, 11, 'S', 'E', 'L', 'E', 'C', 'T', 0};

    assert_int_equal(wire_writer_put_u8(w, 'C'), 0);
    assert_int_equal(wire_writer_put_i32(w, 0), 0);
    assert_int_equal(wire_writer_put_cstring(w, "SELECT"), 0);
    assert_int_equal(wire_writer_patch_i32(w, 1, (int32_t) (wire_writer_size(w) - 1)), 0);
    assert_writer_holds(w, expected, sizeof(expected));
    wire_writer_destroy(w);
}

static void test_writer_truncate_discards_tail([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer();
    const uint8_t expected[] = {1, 2, 9};

    assert_int_equal(wire_writer_put_bytes(w, "\1\2\3\4", 4), 0);
    wire_writer_truncate(w, 2);
    wire_writer_truncate(w, 100);
    wire_writer_truncate(nullptr, 0);
    assert_int_equal(wire_writer_size(w), 2);
    assert_int_equal(wire_writer_put_u8(w, 9), 0);
    assert_writer_holds(w, expected, sizeof(expected));
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

static void test_writer_destroy_accepts_null([[maybe_unused]] void **state)
{
    wire_writer_destroy(nullptr);
}

static void test_encode_functions_write_big_endian([[maybe_unused]] void **state)
{
    uint8_t buf[6] = {};
    const uint8_t expected[] = {0xFF, 0xFE, 0x12, 0x34, 0x56, 0x78};

    wire_encode_i16(buf, -2);
    wire_encode_i32(buf + 2, 0x12345678);
    assert_memory_equal(buf, expected, sizeof(expected));
}

static void test_writer_accessors_accept_null([[maybe_unused]] void **state)
{
    assert_null(wire_writer_data(nullptr));
    assert_int_equal(wire_writer_size(nullptr), 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_writer_encodes_big_endian),
        cmocka_unit_test(test_writer_cstring_includes_terminator),
        cmocka_unit_test(test_writer_grows_past_initial_capacity),
        cmocka_unit_test(test_writer_rejects_size_over_protocol_limit),
        cmocka_unit_test(test_writer_patches_bytes_in_place),
        cmocka_unit_test(test_writer_patch_rejects_out_of_range),
        cmocka_unit_test(test_writer_back_patches_message_length),
        cmocka_unit_test(test_writer_truncate_discards_tail),
        cmocka_unit_test(test_writer_encodes_negative_values),
        cmocka_unit_test(test_encode_functions_write_big_endian),
        cmocka_unit_test(test_writer_accepts_empty_bytes),
        cmocka_unit_test(test_writer_rejects_null_input),
        cmocka_unit_test(test_writer_destroy_accepts_null),
        cmocka_unit_test(test_writer_accessors_accept_null),
    };
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
