/**
 * Tests for the wire protocol input buffer
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

static wire_reader_t *create_reader(const void *data, size_t size)
{
    wire_reader_t *reader = wire_reader_create(data, size);
    if (reader == nullptr) {
        fail();
    }
    return reader;
}

static void assert_failed_with(int result, int expected_errno)
{
    assert_int_equal(result, -1);
    assert_int_equal(errno, expected_errno);
}

static void test_reader_decodes_big_endian([[maybe_unused]] void **state)
{
    const uint8_t input[] = {0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0x80, 0x00};
    wire_reader_t *r = create_reader(input, sizeof(input));
    uint8_t u8 = 0;
    int16_t i16 = 0;
    int32_t i32 = 0;

    assert_int_equal(wire_reader_get_u8(r, &u8), 0);
    assert_int_equal(wire_reader_get_i16(r, &i16), 0);
    assert_int_equal(wire_reader_get_i32(r, &i32), 0);
    assert_int_equal(u8, 0xFF);
    assert_int_equal(i16, -2);
    assert_int_equal(i32, -32768);
    assert_int_equal(wire_reader_remaining(r), 0);
    wire_reader_destroy(r);
}

static void test_reader_fails_on_truncated_input([[maybe_unused]] void **state)
{
    const uint8_t input[] = {0x00, 0x01, 0x02};
    wire_reader_t *r = create_reader(input, sizeof(input));
    int32_t i32 = 0;
    int16_t i16 = 0;

    errno = 0;
    assert_failed_with(wire_reader_get_i32(r, &i32), EBADMSG);
    assert_int_equal(wire_reader_remaining(r), 3);
    assert_int_equal(wire_reader_get_i16(r, &i16), 0);
    assert_int_equal(i16, 1);
    errno = 0;
    assert_failed_with(wire_reader_get_i16(r, &i16), EBADMSG);
    wire_reader_destroy(r);
}

static void test_reader_cstring_returns_view_into_input([[maybe_unused]] void **state)
{
    const uint8_t input[] = {'a', 'b', 0, 0, 'c'};
    wire_reader_t *r = create_reader(input, sizeof(input));
    const char *s = nullptr;

    assert_int_equal(wire_reader_get_cstring(r, &s), 0);
    assert_ptr_equal(s, input);
    assert_string_equal(s, "ab");
    assert_int_equal(wire_reader_get_cstring(r, &s), 0);
    assert_string_equal(s, "");
    errno = 0;
    assert_failed_with(wire_reader_get_cstring(r, &s), EBADMSG);
    assert_int_equal(wire_reader_remaining(r), 1);
    wire_reader_destroy(r);
}

static void test_reader_get_bytes_checks_bounds([[maybe_unused]] void **state)
{
    const uint8_t input[] = {1, 2, 3, 4};
    wire_reader_t *r = create_reader(input, sizeof(input));
    uint8_t out[4] = {};

    errno = 0;
    assert_failed_with(wire_reader_get_bytes(r, out, 5), EBADMSG);
    errno = 0;
    assert_failed_with(wire_reader_get_bytes(r, out, SIZE_MAX), EBADMSG);
    assert_int_equal(wire_reader_get_bytes(r, out, 4), 0);
    assert_memory_equal(out, input, sizeof(input));
    assert_int_equal(wire_reader_get_bytes(r, nullptr, 0), 0);
    wire_reader_destroy(r);
}

static void test_reader_over_empty_input([[maybe_unused]] void **state)
{
    wire_reader_t *r = create_reader(nullptr, 0);
    uint8_t byte = 0;
    const char *s = nullptr;

    assert_int_equal(wire_reader_remaining(r), 0);
    errno = 0;
    assert_failed_with(wire_reader_get_u8(r, &byte), EBADMSG);
    errno = 0;
    assert_failed_with(wire_reader_get_cstring(r, &s), EBADMSG);
    wire_reader_destroy(r);
}

static void test_reader_create_rejects_null_data_with_size([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(wire_reader_create(nullptr, 1));
    assert_int_equal(errno, EINVAL);
}

static void test_reader_rejects_null_output_arguments([[maybe_unused]] void **state)
{
    const uint8_t input[] = {1, 2, 3, 4, 5, 6, 7};
    wire_reader_t *r = create_reader(input, sizeof(input));

    errno = 0;
    assert_failed_with(wire_reader_get_u8(r, nullptr), EINVAL);
    errno = 0;
    assert_failed_with(wire_reader_get_i16(r, nullptr), EINVAL);
    errno = 0;
    assert_failed_with(wire_reader_get_i32(r, nullptr), EINVAL);
    errno = 0;
    assert_failed_with(wire_reader_get_bytes(r, nullptr, 1), EINVAL);
    errno = 0;
    assert_failed_with(wire_reader_get_cstring(r, nullptr), EINVAL);
    assert_int_equal(wire_reader_remaining(r), sizeof(input));
    wire_reader_destroy(r);
}

static void test_reader_rejects_null_reader([[maybe_unused]] void **state)
{
    uint8_t byte = 0;
    const char *s = nullptr;

    errno = 0;
    assert_failed_with(wire_reader_get_u8(nullptr, &byte), EINVAL);
    errno = 0;
    assert_failed_with(wire_reader_get_cstring(nullptr, &s), EINVAL);
    assert_int_equal(wire_reader_remaining(nullptr), 0);
    wire_reader_destroy(nullptr);
}

typedef enum { OP_U8, OP_I16, OP_I32, OP_CSTRING, OP_COUNT } op_kind_t;

typedef struct model_op {
    op_kind_t kind;
    int32_t number;
    char text[16];
} model_op_t;

static constexpr uint32_t MODEL_SEED = 12345;

static uint32_t next_random32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static model_op_t random_op(uint32_t *rng)
{
    model_op_t op = {.kind = (op_kind_t) (next_random32(rng) % OP_COUNT)};
    size_t len = next_random32(rng) % 15;

    op.number = (int32_t) next_random32(rng);
    for (size_t i = 0; i < len; i++) {
        op.text[i] = (char) ('a' + (next_random32(rng) % 26));
    }
    return op;
}

static void write_op(wire_writer_t *w, const model_op_t *op)
{
    int result = 0;
    switch (op->kind) {
        case OP_U8:
            result = wire_writer_put_u8(w, (uint8_t) op->number);
            break;
        case OP_I16:
            result = wire_writer_put_i16(w, (int16_t) op->number);
            break;
        case OP_I32:
            result = wire_writer_put_i32(w, op->number);
            break;
        default:
            result = wire_writer_put_cstring(w, op->text);
            break;
    }
    assert_int_equal(result, 0);
}

static void check_integer_op(wire_reader_t *r, const model_op_t *op)
{
    uint8_t u8 = 0;
    int16_t i16 = 0;
    int32_t i32 = 0;

    if (op->kind == OP_U8) {
        assert_int_equal(wire_reader_get_u8(r, &u8), 0);
        assert_int_equal(u8, (uint8_t) op->number);
    } else if (op->kind == OP_I16) {
        assert_int_equal(wire_reader_get_i16(r, &i16), 0);
        assert_int_equal(i16, (int16_t) op->number);
    } else {
        assert_int_equal(wire_reader_get_i32(r, &i32), 0);
        assert_int_equal(i32, op->number);
    }
}

static void check_op(wire_reader_t *r, const model_op_t *op)
{
    const char *s = nullptr;

    if (op->kind != OP_CSTRING) {
        check_integer_op(r, op);
        return;
    }
    assert_int_equal(wire_reader_get_cstring(r, &s), 0);
    assert_string_equal(s, op->text);
}

static void test_random_sequence_round_trips_through_writer([[maybe_unused]] void **state)
{
    enum { N_OPS = 2000 };
    static model_op_t ops[N_OPS];
    wire_writer_t *w = wire_writer_create();
    uint32_t rng = MODEL_SEED;

    for (int i = 0; i < N_OPS; i++) {
        ops[i] = random_op(&rng);
        write_op(w, &ops[i]);
    }
    wire_reader_t *r = create_reader(wire_writer_data(w), wire_writer_size(w));
    for (int i = 0; i < N_OPS; i++) {
        check_op(r, &ops[i]);
    }
    assert_int_equal(wire_reader_remaining(r), 0);
    wire_reader_destroy(r);
    wire_writer_destroy(w);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reader_decodes_big_endian),
        cmocka_unit_test(test_reader_fails_on_truncated_input),
        cmocka_unit_test(test_reader_cstring_returns_view_into_input),
        cmocka_unit_test(test_reader_get_bytes_checks_bounds),
        cmocka_unit_test(test_reader_over_empty_input),
        cmocka_unit_test(test_reader_create_rejects_null_data_with_size),
        cmocka_unit_test(test_reader_rejects_null_output_arguments),
        cmocka_unit_test(test_reader_rejects_null_reader),
        cmocka_unit_test(test_random_sequence_round_trips_through_writer),
    };
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
