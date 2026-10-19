/**
 * Tests for CRC-32C and for the checks of a page read from disk
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "storage/crc32c.h"
#include "storage/page.h"

// Where the fields of the page header and of the first slot are, as the format defines them
static constexpr size_t CHECKSUM_OFFSET = 8;
static constexpr size_t TYPE_OFFSET = 12;
static constexpr size_t SLOT_COUNT_OFFSET = 16;
static constexpr size_t UPPER_OFFSET = 18;
static constexpr size_t FIRST_SLOT_OFFSET = 24;
static constexpr uint16_t DEAD_STATE_BITS = 2U << 14;
static constexpr uint16_t BAD_STATE_BITS = 3U << 14;

static void poke16(page_t *page, size_t offset, uint16_t value)
{
    memcpy(page->bytes + offset, &value, sizeof(value));
}

static page_t page_with_tuples(void)
{
    page_t page;
    page_init(&page, PAGE_TYPE_HEAP);
    assert_int_equal(page_insert(&page, "alpha", 5), 0);
    assert_int_equal(page_insert(&page, "beta", 4), 1);
    assert_int_equal(page_delete(&page, 1), 0);
    return page;
}

static void assert_corrupt(const page_t *page)
{
    errno = 0;
    assert_int_equal(page_validate(page), -1);
    assert_int_equal(errno, EBADMSG);
}

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
    unsigned char bytes[PAGE_SIZE];

    for (size_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (unsigned char) ((i * 31) + 7);
    }
    assert_int_equal(crc32c(bytes, sizeof(bytes)), 0x29202EFC);
}

static void test_a_page_with_its_checksum_verifies([[maybe_unused]] void **state)
{
    page_t page = page_with_tuples();

    errno = 0;
    assert_int_equal(page_verify_checksum(&page), -1);
    assert_int_equal(errno, EBADMSG);
    page_set_checksum(&page);
    assert_int_equal(page_verify_checksum(&page), 0);
    page_set_checksum(&page);
    assert_int_equal(page_verify_checksum(&page), 0);
}

static void test_any_changed_byte_breaks_the_checksum([[maybe_unused]] void **state)
{
    page_t page = page_with_tuples();

    page_set_checksum(&page);
    for (size_t offset = 0; offset < PAGE_SIZE; offset += 97) {
        page.bytes[offset] ^= 0x10;
        assert_int_equal(page_verify_checksum(&page), -1);
        page.bytes[offset] ^= 0x10;
    }
    assert_int_equal(page_verify_checksum(&page), 0);
    page.bytes[CHECKSUM_OFFSET] ^= 0x01;
    assert_int_equal(page_verify_checksum(&page), -1);
}

static void test_pages_made_by_the_page_functions_are_valid([[maybe_unused]] void **state)
{
    page_t page;

    page_init(&page, PAGE_TYPE_FREE);
    assert_int_equal(page_validate(&page), 0);
    page = page_with_tuples();
    assert_int_equal(page_validate(&page), 0);
    assert_int_equal(page_reclaim_dead_slots(&page), 1);
    assert_int_equal(page_validate(&page), 0);
}

static void test_a_page_of_zeros_is_not_valid([[maybe_unused]] void **state)
{
    page_t page = {};

    assert_corrupt(&page);
}

static void test_a_bad_header_is_corrupt([[maybe_unused]] void **state)
{
    page_t page = page_with_tuples();

    poke16(&page, TYPE_OFFSET, PAGE_TYPE_COUNT);
    assert_corrupt(&page);
    page = page_with_tuples();
    poke16(&page, UPPER_OFFSET, PAGE_SIZE + 1);
    assert_corrupt(&page);
    page = page_with_tuples();
    poke16(&page, SLOT_COUNT_OFFSET, UINT16_MAX);
    assert_corrupt(&page);
}

static void test_a_bad_slot_is_corrupt([[maybe_unused]] void **state)
{
    page_t page = page_with_tuples();
    const uint16_t good_offset = PAGE_SIZE - 5;

    poke16(&page, FIRST_SLOT_OFFSET, BAD_STATE_BITS);
    assert_corrupt(&page);
    page = page_with_tuples();
    poke16(&page, FIRST_SLOT_OFFSET, (uint16_t) ((1U << 14) | (good_offset - 100)));
    assert_corrupt(&page);
    page = page_with_tuples();
    poke16(&page, FIRST_SLOT_OFFSET + 2, 6);
    assert_corrupt(&page);
    page = page_with_tuples();
    poke16(&page, FIRST_SLOT_OFFSET, DEAD_STATE_BITS);
    assert_int_equal(page_validate(&page), 0);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_crc32c_matches_the_published_check_values),
    cmocka_unit_test(test_crc32c_matches_longer_text),
    cmocka_unit_test(test_crc32c_matches_a_page_sized_input),
    cmocka_unit_test(test_a_page_with_its_checksum_verifies),
    cmocka_unit_test(test_any_changed_byte_breaks_the_checksum),
    cmocka_unit_test(test_pages_made_by_the_page_functions_are_valid),
    cmocka_unit_test(test_a_page_of_zeros_is_not_valid),
    cmocka_unit_test(test_a_bad_header_is_corrupt),
    cmocka_unit_test(test_a_bad_slot_is_corrupt),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
