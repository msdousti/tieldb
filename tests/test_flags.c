/**
 * Tests for the flag tests
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "common/flags.h"

static void test_are_set_needs_every_bit_of_the_mask([[maybe_unused]] void **state)
{
    assert_true(flags_are_set(0b1011, 0b0011));
    assert_true(flags_are_set(0b1011, 0b1011));
    assert_false(flags_are_set(0b1011, 0b0111));
    assert_false(flags_are_set(0, 0b1));
}

static void test_an_empty_mask_is_always_set_and_clear([[maybe_unused]] void **state)
{
    assert_true(flags_are_set(0b1011, 0));
    assert_true(flags_are_clear(0b1011, 0));
}

static void test_are_clear_needs_no_bit_of_the_mask([[maybe_unused]] void **state)
{
    assert_true(flags_are_clear(0b1011, 0b0100));
    assert_false(flags_are_clear(0b1011, 0b0110));
    assert_false(flags_are_clear(0b1011, 0b1011));
}

static void test_the_whole_word_is_tested([[maybe_unused]] void **state)
{
    assert_true(flags_are_set(UINT64_MAX, UINT64_C(1) << 63));
    assert_false(flags_are_clear(UINT64_C(1) << 63, UINT64_MAX));
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_are_set_needs_every_bit_of_the_mask),
    cmocka_unit_test(test_an_empty_mask_is_always_set_and_clear),
    cmocka_unit_test(test_are_clear_needs_no_bit_of_the_mask),
    cmocka_unit_test(test_the_whole_word_is_tested),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
