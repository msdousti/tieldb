/**
 * Tests for the heap tuple header
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "storage/heap_tuple.h"
#include "storage/page.h"
#include "test_random.h"

static constexpr size_t MAX_ROW = 64;

static void assert_fails_with(ssize_t status, int error)
{
    assert_true(status < 0);
    assert_int_equal(errno, error);
}

static void test_a_tuple_without_ids_costs_two_bytes([[maybe_unused]] void **state)
{
    const heap_tuple_t tuple = {.row = "row", .row_length = 3};
    unsigned char bytes[8];
    heap_tuple_t decoded;

    assert_int_equal(heap_tuple_size(&tuple), 5);
    assert_int_equal(heap_tuple_encode(&tuple, bytes, sizeof(bytes)), 5);
    assert_int_equal(heap_tuple_decode(bytes, 5, &decoded), 0);
    assert_int_equal(decoded.flags, 0);
    assert_int_equal(decoded.xmin, 0);
    assert_int_equal(decoded.xmax, 0);
    assert_int_equal(decoded.row_length, 3);
    assert_memory_equal(decoded.row, "row", 3);
}

static void test_ids_are_kept_when_the_flag_is_set([[maybe_unused]] void **state)
{
    const heap_tuple_t tuple = {.flags = HEAP_TUPLE_HAS_XIDS, .xmin = 7, .xmax = UINT64_MAX};
    unsigned char bytes[32];
    heap_tuple_t decoded;

    assert_int_equal(heap_tuple_size(&tuple), 18);
    assert_int_equal(heap_tuple_encode(&tuple, bytes, sizeof(bytes)), 18);
    assert_int_equal(heap_tuple_decode(bytes, 18, &decoded), 0);
    assert_int_equal(decoded.xmin, 7);
    assert_int_equal(decoded.xmax, UINT64_MAX);
    assert_int_equal(decoded.row_length, 0);
}

static void test_ids_are_ignored_without_the_flag([[maybe_unused]] void **state)
{
    const heap_tuple_t tuple = {.xmin = 7, .xmax = 9};
    unsigned char bytes[2];
    heap_tuple_t decoded;

    assert_int_equal(heap_tuple_encode(&tuple, bytes, sizeof(bytes)), 2);
    assert_int_equal(heap_tuple_decode(bytes, 2, &decoded), 0);
    assert_int_equal(decoded.xmin, 0);
    assert_int_equal(decoded.xmax, 0);
}

static void test_encoding_rejects_reserved_flags_and_small_buffers([[maybe_unused]] void **state)
{
    const heap_tuple_t reserved = {.flags = 1U << 1};
    const heap_tuple_t tuple = {.row = "row", .row_length = 3};
    unsigned char bytes[4];

    assert_fails_with(heap_tuple_size(&reserved), EINVAL);
    assert_fails_with(heap_tuple_encode(&reserved, bytes, sizeof(bytes)), EINVAL);
    assert_fails_with(heap_tuple_encode(&tuple, bytes, sizeof(bytes)), E2BIG);
}

static void test_decoding_rejects_malformed_bytes([[maybe_unused]] void **state)
{
    unsigned char reserved[2] = {};
    unsigned char with_ids[8] = {};
    heap_tuple_t decoded;

    reserved[0] = 1U << 1;
    with_ids[0] = HEAP_TUPLE_HAS_XIDS;
    assert_fails_with(heap_tuple_decode(reserved, 0, &decoded), EBADMSG);
    assert_fails_with(heap_tuple_decode(reserved, 1, &decoded), EBADMSG);
    assert_fails_with(heap_tuple_decode(reserved, sizeof(reserved), &decoded), EBADMSG);
    assert_fails_with(heap_tuple_decode(with_ids, sizeof(with_ids), &decoded), EBADMSG);
}

static heap_tuple_t random_tuple(uint64_t *random, unsigned char *row)
{
    for (size_t b = 0; b < MAX_ROW; b++) {
        row[b] = (unsigned char) next_random(random);
    }
    heap_tuple_t tuple = {.row = row, .row_length = next_random(random) % (MAX_ROW + 1)};
    if ((next_random(random) & 1U) != 0) {
        tuple.flags = HEAP_TUPLE_HAS_XIDS;
        tuple.xmin = next_random(random);
        tuple.xmax = next_random(random);
    }
    return tuple;
}

static void assert_same_tuple(const heap_tuple_t *actual, const heap_tuple_t *expected)
{
    assert_int_equal(actual->flags, expected->flags);
    assert_int_equal(actual->xmin, expected->xmin);
    assert_int_equal(actual->xmax, expected->xmax);
    assert_int_equal(actual->row_length, expected->row_length);
    assert_memory_equal(actual->row, expected->row, expected->row_length);
}

static void round_trip_random_tuples(uint64_t seed)
{
    uint64_t random = seed;
    unsigned char row[MAX_ROW];
    unsigned char bytes[PAGE_SIZE];

    print_message("seed %llu\n", (unsigned long long) seed);
    for (int i = 0; i < 200; i++) {
        heap_tuple_t tuple = random_tuple(&random, row);
        heap_tuple_t decoded;
        ssize_t size = heap_tuple_encode(&tuple, bytes, sizeof(bytes));

        assert_true(size > 0);
        assert_int_equal(heap_tuple_decode(bytes, (size_t) size, &decoded), 0);
        assert_same_tuple(&decoded, &tuple);
    }
}

static void test_random_tuples_survive_a_round_trip([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261025};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        round_trip_random_tuples(seeds[i]);
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_a_tuple_without_ids_costs_two_bytes),
    cmocka_unit_test(test_ids_are_kept_when_the_flag_is_set),
    cmocka_unit_test(test_ids_are_ignored_without_the_flag),
    cmocka_unit_test(test_encoding_rejects_reserved_flags_and_small_buffers),
    cmocka_unit_test(test_decoding_rejects_malformed_bytes),
    cmocka_unit_test(test_random_tuples_survive_a_round_trip),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
