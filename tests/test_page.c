/**
 * Tests for slotted pages
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "storage/page.h"

static constexpr size_t HEADER_SIZE = 24;
static constexpr size_t SLOT_SIZE = 4;

static page_t page;

static int new_page([[maybe_unused]] void **state)
{
    page_init(&page, PAGE_TYPE_HEAP);
    return 0;
}
static void assert_tuple_is(size_t slot, const void *expected, size_t expected_length)
{
    size_t length = 0;
    const void *tuple = page_get(&page, slot, &length);
    if (tuple == nullptr) {
        fail();
    }
    assert_int_equal(length, expected_length);
    assert_memory_equal(tuple, expected, expected_length);
}

static void assert_slot_is_missing(size_t slot)
{
    size_t length = 0;
    errno = 0;
    assert_null(page_get(&page, slot, &length));
    assert_int_equal(errno, ENOENT);
}

static void test_new_page_is_empty([[maybe_unused]] void **state)
{
    assert_int_equal(page_type(&page), PAGE_TYPE_HEAP);
    assert_int_equal(page_slot_count(&page), 0);
    assert_int_equal(page_free_space(&page), PAGE_SIZE - HEADER_SIZE - SLOT_SIZE);
}

static void test_inserted_tuples_are_returned_unchanged([[maybe_unused]] void **state)
{
    assert_int_equal(page_insert(&page, "alpha", 5), 0);
    assert_int_equal(page_insert(&page, "be", 2), 1);

    assert_int_equal(page_slot_count(&page), 2);
    assert_tuple_is(0, "alpha", 5);
    assert_tuple_is(1, "be", 2);
}

static void test_an_empty_tuple_can_be_stored([[maybe_unused]] void **state)
{
    assert_int_equal(page_insert(&page, "", 0), 0);
    assert_int_equal(page_insert(&page, "x", 1), 1);

    assert_tuple_is(0, "", 0);
    assert_tuple_is(1, "x", 1);
}

static void test_missing_slots_are_reported([[maybe_unused]] void **state)
{
    assert_int_equal(page_insert(&page, "a", 1), 0);
    assert_int_equal(page_delete(&page, 0), 0);

    assert_slot_is_missing(0);
    assert_slot_is_missing(1);
    errno = 0;
    assert_int_equal(page_delete(&page, 0), -1);
    assert_int_equal(errno, ENOENT);
    errno = 0;
    assert_int_equal(page_delete(&page, 7), -1);
    assert_int_equal(errno, ENOENT);
}

static void
test_a_deleted_slot_number_is_reused_only_after_reclaiming([[maybe_unused]] void **state)
{
    for (int i = 0; i < 4; i++) {
        assert_int_equal(page_insert(&page, "tuple", 5), i);
    }
    assert_int_equal(page_delete(&page, 2), 0);
    assert_int_equal(page_delete(&page, 1), 0);
    assert_int_equal(page_insert(&page, "new", 3), 4);

    assert_int_equal(page_reclaim_dead_slots(&page), 2);
    assert_int_equal(page_reclaim_dead_slots(&page), 0);
    assert_int_equal(page_insert(&page, "newer", 5), 1);
    assert_int_equal(page_insert(&page, "last", 4), 2);
    assert_tuple_is(1, "newer", 5);
    assert_tuple_is(3, "tuple", 5);
}

static void test_a_deleted_tuple_frees_its_space_at_once([[maybe_unused]] void **state)
{
    unsigned char big[PAGE_SIZE] = {};
    size_t largest = page_free_space(&page);

    assert_int_equal(page_insert(&page, big, largest), 0);
    assert_int_equal(page_delete(&page, 0), 0);

    assert_int_equal(page_free_space(&page), largest - SLOT_SIZE);
    assert_int_equal(page_insert(&page, big, largest - SLOT_SIZE), 1);
}

static void test_page_flags_are_kept_for_the_engine([[maybe_unused]] void **state)
{
    assert_int_equal(page_flags(&page), 0);

    page_set_flags(&page, 0xa5c3);
    assert_int_equal(page_insert(&page, "x", 1), 0);

    assert_int_equal(page_flags(&page), 0xa5c3);
    assert_int_equal(page_type(&page), PAGE_TYPE_HEAP);
}

static void test_the_largest_tuple_fills_the_page([[maybe_unused]] void **state)
{
    unsigned char big[PAGE_SIZE] = {};
    size_t largest = page_free_space(&page);

    errno = 0;
    assert_int_equal(page_insert(&page, big, largest + 1), -1);
    assert_int_equal(errno, ENOSPC);
    assert_int_equal(page_insert(&page, big, largest), 0);
    assert_int_equal(page_free_space(&page), 0);
    errno = 0;
    assert_int_equal(page_insert(&page, "", 0), -1);
    assert_int_equal(errno, ENOSPC);
}

static void test_scattered_free_space_is_gathered_when_needed([[maybe_unused]] void **state)
{
    unsigned char tuple[800];
    unsigned char large[2400];

    for (size_t i = 0; i < 10; i++) {
        memset(tuple, (int) ('a' + i), sizeof(tuple));
        assert_int_equal(page_insert(&page, tuple, sizeof(tuple)), (int) i);
    }
    for (size_t i = 0; i < 10; i += 2) {
        assert_int_equal(page_delete(&page, i), 0);
    }
    memset(large, 'z', sizeof(large));

    assert_int_equal(page_insert(&page, large, sizeof(large)), 10);
    assert_tuple_is(10, large, sizeof(large));
    for (size_t i = 1; i < 10; i += 2) {
        memset(tuple, (int) ('a' + i), sizeof(tuple));
        assert_tuple_is(i, tuple, sizeof(tuple));
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup(test_new_page_is_empty, new_page),
    cmocka_unit_test_setup(test_inserted_tuples_are_returned_unchanged, new_page),
    cmocka_unit_test_setup(test_an_empty_tuple_can_be_stored, new_page),
    cmocka_unit_test_setup(test_missing_slots_are_reported, new_page),
    cmocka_unit_test_setup(test_a_deleted_slot_number_is_reused_only_after_reclaiming, new_page),
    cmocka_unit_test_setup(test_a_deleted_tuple_frees_its_space_at_once, new_page),
    cmocka_unit_test_setup(test_page_flags_are_kept_for_the_engine, new_page),
    cmocka_unit_test_setup(test_the_largest_tuple_fills_the_page, new_page),
    cmocka_unit_test_setup(test_scattered_free_space_is_gathered_when_needed, new_page),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
