/**
 * Tests for the row codec
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "storage/page.h"
#include "storage/row_codec.h"
#include "test_random.h"

static const type_info_t *const INT_TEXT[] = {&TYPE_INT4, &TYPE_TEXT};
static const row_layout_t PAIR = {INT_TEXT, 2};

static row_value_t text_value(const char *text)
{
    return (row_value_t){text, strlen(text)};
}

static row_value_t int_value(const int32_t *number)
{
    return (row_value_t){number, sizeof(*number)};
}

static void assert_value_is(row_value_t actual, row_value_t expected)
{
    assert_int_equal(actual.data == nullptr, expected.data == nullptr);
    assert_int_equal(actual.length, expected.length);
    if (expected.data != nullptr) {
        assert_memory_equal(actual.data, expected.data, expected.length);
    }
}

static void assert_round_trip(const row_layout_t *layout, const row_value_t *values)
{
    unsigned char row[PAGE_SIZE * 8];
    row_value_t decoded[32];
    ssize_t size = row_encode(layout, values, row, sizeof(row));

    assert_int_equal(size, row_size(layout, values));
    assert_int_equal(row_decode(layout, row, (size_t) size, decoded), 0);
    for (size_t i = 0; i < layout->count; i++) {
        assert_value_is(decoded[i], values[i]);
    }
    for (size_t shorter = 0; shorter < (size_t) size; shorter++) {
        errno = 0;
        assert_int_equal(row_decode(layout, row, shorter, decoded), -1);
        assert_int_equal(errno, EINVAL);
    }
}

static void test_a_row_without_columns_has_no_bytes([[maybe_unused]] void **state)
{
    const row_layout_t none = {nullptr, 0};
    row_value_t decoded[1];
    unsigned char row[1];

    assert_int_equal(row_size(&none, nullptr), 0);
    assert_int_equal(row_encode(&none, nullptr, row, 0), 0);
    assert_int_equal(row_decode(&none, row, 0, decoded), 0);
}

static void test_values_are_laid_out_after_the_null_bitmap([[maybe_unused]] void **state)
{
    const int32_t number = 7;
    const row_value_t values[] = {int_value(&number), text_value("Rex")};
    unsigned char row[16];
    const unsigned char expected[] = {0x00, 7, 0, 0, 0, 3, 0, 'R', 'e', 'x'};

    assert_int_equal(row_encode(&PAIR, values, row, sizeof(row)), sizeof(expected));
    assert_memory_equal(row, expected, sizeof(expected));
    assert_round_trip(&PAIR, values);
}

static void test_null_and_empty_values_are_different([[maybe_unused]] void **state)
{
    const row_value_t nulls[] = {{}, {}};
    const row_value_t empty[] = {{}, text_value("")};
    unsigned char row[16];

    assert_int_equal(row_encode(&PAIR, nulls, row, sizeof(row)), 1);
    assert_int_equal(row[0], 0x03);
    assert_int_equal(row_encode(&PAIR, empty, row, sizeof(row)), 3);
    assert_int_equal(row[0], 0x01);
    assert_round_trip(&PAIR, nulls);
    assert_round_trip(&PAIR, empty);
}

static void test_the_bitmap_grows_with_the_columns([[maybe_unused]] void **state)
{
    const type_info_t *types[10];
    row_value_t values[10] = {};
    const row_layout_t wide = {types, 10};
    unsigned char row[32];

    for (size_t i = 0; i < 10; i++) {
        types[i] = &TYPE_TEXT;
        values[i] = (i == 0 || i == 7 || i >= 8) ? (row_value_t){} : text_value("x");
    }
    assert_int_equal(row_encode(&wide, values, row, sizeof(row)), 2 + (6 * 3));
    assert_int_equal(row[0], 0x81);
    assert_int_equal(row[1], 0x03);
    assert_round_trip(&wide, values);
}

static void test_text_is_limited_to_the_length_that_fits_the_prefix([[maybe_unused]] void **state)
{
    static char longest[ROW_MAX_VALUE_LENGTH + 1];
    row_value_t value[2] = {{}, {longest, ROW_MAX_VALUE_LENGTH}};
    unsigned char row[PAGE_SIZE * 8];

    memset(longest, 'a', sizeof(longest));
    assert_round_trip(&PAIR, value);
    value[1].length++;
    errno = 0;
    assert_int_equal(row_size(&PAIR, value), -1);
    assert_int_equal(errno, EFBIG);
    assert_int_equal(row_encode(&PAIR, value, row, sizeof(row)), -1);
    assert_int_equal(errno, EFBIG);
}

static void test_a_fixed_size_value_must_have_the_size_of_its_type([[maybe_unused]] void **state)
{
    const row_value_t values[] = {text_value("12"), text_value("x")};

    errno = 0;
    assert_int_equal(row_size(&PAIR, values), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_the_row_must_fit_the_buffer([[maybe_unused]] void **state)
{
    const int32_t number = 1;
    const row_value_t values[] = {int_value(&number), text_value("abc")};
    unsigned char row[16];

    errno = 0;
    assert_int_equal(row_encode(&PAIR, values, row, 9), -1);
    assert_int_equal(errno, E2BIG);
    assert_int_equal(row_encode(&PAIR, values, row, 10), 10);
}

static void test_a_malformed_row_is_rejected([[maybe_unused]] void **state)
{
    const int32_t number = 1;
    const row_value_t values[] = {int_value(&number), text_value("abc")};
    unsigned char row[16];
    row_value_t decoded[2];

    assert_int_equal(row_encode(&PAIR, values, row, sizeof(row)), 10);
    row[10] = 0;
    errno = 0;
    assert_int_equal(row_decode(&PAIR, row, 11, decoded), -1);
    assert_int_equal(errno, EINVAL);
    row[6] = 0x80;
    errno = 0;
    assert_int_equal(row_decode(&PAIR, row, 10, decoded), -1);
    assert_int_equal(errno, EINVAL);
}

static constexpr size_t MAX_COLUMNS = 12;
static constexpr size_t MAX_TEXT = 100;

static void random_row(uint64_t *random, const type_info_t **types, row_value_t *values,
                       size_t count)
{
    static int32_t numbers[MAX_COLUMNS];
    static char texts[MAX_COLUMNS][MAX_TEXT];

    for (size_t i = 0; i < count; i++) {
        size_t length = next_random(random) % MAX_TEXT;
        types[i] = next_random(random) % 2 == 0 ? &TYPE_INT4 : &TYPE_TEXT;
        numbers[i] = (int32_t) next_random(random);
        memset(texts[i], 'a' + (int) i, length);
        values[i] =
            types[i] == &TYPE_INT4 ? int_value(&numbers[i]) : (row_value_t){texts[i], length};
        values[i] = next_random(random) % 4 == 0 ? (row_value_t){} : values[i];
    }
}

static void round_trip_random_rows(uint64_t seed)
{
    uint64_t random = seed;

    print_message("seed %llu\n", (unsigned long long) seed);
    for (size_t round = 0; round < 500; round++) {
        const type_info_t *types[MAX_COLUMNS];
        row_value_t values[MAX_COLUMNS];
        size_t count = next_random(&random) % (MAX_COLUMNS + 1);
        const row_layout_t layout = {types, count};

        random_row(&random, types, values, count);
        assert_round_trip(&layout, values);
    }
}

static void test_random_rows_survive_a_round_trip([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261020};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        round_trip_random_rows(seeds[i]);
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_a_row_without_columns_has_no_bytes),
    cmocka_unit_test(test_values_are_laid_out_after_the_null_bitmap),
    cmocka_unit_test(test_null_and_empty_values_are_different),
    cmocka_unit_test(test_the_bitmap_grows_with_the_columns),
    cmocka_unit_test(test_text_is_limited_to_the_length_that_fits_the_prefix),
    cmocka_unit_test(test_a_fixed_size_value_must_have_the_size_of_its_type),
    cmocka_unit_test(test_the_row_must_fit_the_buffer),
    cmocka_unit_test(test_a_malformed_row_is_rejected),
    cmocka_unit_test(test_random_rows_survive_a_round_trip),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
