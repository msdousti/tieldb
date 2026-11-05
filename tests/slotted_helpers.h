/**
 * Calls on a slotted page_t for tests that use a page but are not about slotted pages. Each call
 * opens the page, so a page of the wrong kind fails the test.
 */

#pragma once

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "storage/slotted_page.h"

static inline slotted_page_t test_slotted_view(const page_t *page)
{
    slotted_page_t slotted;
    // The view is only read through the const functions, or written by the caller's own page
    assert_int_equal(slotted_page_open_any(&slotted, (page_t *) page), 0);
    return slotted;
}

static inline int test_slotted_insert(page_t *page, const void *tuple, size_t length)
{
    slotted_page_t slotted = test_slotted_view(page);
    return slotted_page_insert(&slotted, tuple, length);
}

static inline const void *test_slotted_get(const page_t *page, size_t slot, size_t *length)
{
    slotted_page_t slotted = test_slotted_view(page);
    return slotted_page_get(&slotted, slot, length);
}

static inline int test_slotted_delete(page_t *page, size_t slot)
{
    slotted_page_t slotted = test_slotted_view(page);
    return slotted_page_delete(&slotted, slot);
}

static inline size_t test_slotted_slot_count(const page_t *page)
{
    slotted_page_t slotted = test_slotted_view(page);
    return slotted_page_slot_count(&slotted);
}

static inline size_t test_slotted_free_space(const page_t *page)
{
    slotted_page_t slotted = test_slotted_view(page);
    return slotted_page_free_space(&slotted);
}
