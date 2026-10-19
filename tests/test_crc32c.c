/**
 * Tests for CRC-32C
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "storage/crc32c.h"

static constexpr size_t PAGE_SIZED_INPUT = 8192;

static void test_crc32c_matches_the_published_check_values([[maybe_unused]] void **state)
{
    unsigned char zeros[32] = {};
    unsigned char ones[32];
    unsigned char ascending[32];
    unsigned char descending[32];

    memset(ones, 0xff, sizeof(ones));
    for (size_t i = 0; i < sizeof(ascending); i++) {
        ascending[i] = (unsigned char) i;
        descending[i] = (unsigned char) (sizeof(descending) - 1 - i);
    }
    assert_int_equal(crc32c("123456789", 9), 0xE3069283);
    assert_int_equal(crc32c(zeros, sizeof(zeros)), 0x8A9136AA);
    assert_int_equal(crc32c(ones, sizeof(ones)), 0x62A8AB43);
    assert_int_equal(crc32c(ascending, sizeof(ascending)), 0x46DD794E);
    assert_int_equal(crc32c(descending, sizeof(descending)), 0x113FDB5C);
    assert_int_equal(crc32c("", 0), 0);
}

// The expected values below were computed with an independent implementation (a bitwise one in
// Python that reproduces every published value above); the sentence is a widely quoted example
static void test_crc32c_matches_longer_text([[maybe_unused]] void **state)
{
    const char *fox = "The quick brown fox jumps over the lazy dog";
    const char *digits = "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789";

    assert_int_equal(crc32c(fox, strlen(fox)), 0x22620404);
    assert_int_equal(strlen(digits), 100);
    assert_int_equal(crc32c(digits, strlen(digits)), 0x90CDF36E);
}

static void test_crc32c_matches_a_page_sized_input([[maybe_unused]] void **state)
{
    unsigned char bytes[PAGE_SIZED_INPUT];

    for (size_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (unsigned char) ((i * 31) + 7);
    }
    assert_int_equal(crc32c(bytes, sizeof(bytes)), 0x29202EFC);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_crc32c_matches_the_published_check_values),
    cmocka_unit_test(test_crc32c_matches_longer_text),
    cmocka_unit_test(test_crc32c_matches_a_page_sized_input),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
