/**
 * Tests for free space map pages
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ds/max_tree.h"
#include "storage/fsm_page.h"
#include "storage/slotted_page.h"

static constexpr size_t HEADER_SIZE = 24;

static page_t page;

static void test_the_body_follows_the_header([[maybe_unused]] void **state)
{
    fsm_page_t fsm;
    size_t length = 0;

    fsm_page_init(&fsm, &page);
    unsigned char *body = fsm_page_body(&fsm, &length);
    assert_int_equal(page_type(&page), PAGE_TYPE_FSM);
    assert_ptr_equal(body, page.bytes + HEADER_SIZE);
    assert_int_equal(length, PAGE_SIZE - HEADER_SIZE);
    for (size_t i = 0; i < length; i++) {
        assert_int_equal(body[i], 0);
    }
}

static void test_any_body_is_valid_and_covered_by_the_checksum([[maybe_unused]] void **state)
{
    fsm_page_t fsm;
    size_t length = 0;

    fsm_page_init(&fsm, &page);
    unsigned char *body = fsm_page_body(&fsm, &length);
    memset(body, 0xA5, length);
    page_set_checksum(&page);
    assert_int_equal(page_validate(&page), 0);
    assert_int_equal(page_verify_checksum(&page), 0);
    body[length - 1] ^= 1;
    errno = 0;
    assert_int_equal(page_verify_checksum(&page), -1);
    assert_int_equal(errno, EBADMSG);
}

static void test_only_a_free_space_map_page_opens([[maybe_unused]] void **state)
{
    fsm_page_t fsm;
    slotted_page_t slotted;

    slotted_page_init(&slotted, &page, SLOTTED_PAGE_HEAP);
    errno = 0;
    assert_int_equal(fsm_page_open(&fsm, &page), -1);
    assert_int_equal(errno, EINVAL);
    fsm_page_init(&fsm, &page);
    assert_int_equal(fsm_page_open(&fsm, &page), 0);
    assert_ptr_equal(fsm.page, &page);
    errno = 0;
    assert_int_equal(slotted_page_open(&slotted, &page, SLOTTED_PAGE_HEAP), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_a_max_tree_in_the_body_survives_a_round_trip([[maybe_unused]] void **state)
{
    static page_t copy;
    fsm_page_t fsm;
    max_tree_t tree;
    size_t found = 0;

    fsm_page_init(&fsm, &page);
    fsm_page_tree(&fsm, &tree);
    assert_int_equal(max_tree_set(&tree, FSM_PAGE_LEAVES - 1, 77), 0);
    page_set_checksum(&page);

    memcpy(copy.bytes, page.bytes, PAGE_SIZE);
    assert_int_equal(page_verify_checksum(&copy), 0);
    assert_int_equal(fsm_page_open(&fsm, &copy), 0);
    fsm_page_tree(&fsm, &tree);
    assert_int_equal(max_tree_root(&tree), 77);
    assert_int_equal(max_tree_find(&tree, 77, &found), 0);
    assert_int_equal(found, FSM_PAGE_LEAVES - 1);
    assert_int_equal(max_tree_set(&tree, FSM_PAGE_LEAVES, 1), -1);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_the_body_follows_the_header),
    cmocka_unit_test(test_any_body_is_valid_and_covered_by_the_checksum),
    cmocka_unit_test(test_only_a_free_space_map_page_opens),
    cmocka_unit_test(test_a_max_tree_in_the_body_survives_a_round_trip),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
