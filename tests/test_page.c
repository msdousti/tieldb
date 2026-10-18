/**
 * Tests for slotted pages
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "storage/page.h"
#include "test_random.h"

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

static constexpr size_t MODEL_SLOTS = 2048;
static constexpr size_t MAX_TUPLE = 400;
static constexpr size_t OPERATIONS = 20000;
static constexpr size_t CHECK_EVERY = 50;
static constexpr uint64_t RECLAIM_ONE_IN = 16;

typedef enum { MODEL_UNUSED, MODEL_NORMAL, MODEL_DEAD } model_state_t;

typedef struct {
    model_state_t state;
    size_t length;
    unsigned char data[MAX_TUPLE];
} model_slot_t;

// The model is an array of tuples; it knows nothing about offsets or free space except the sum
typedef struct {
    model_slot_t slots[MODEL_SLOTS];
    size_t count;
} model_t;

static size_t model_free_slot(const model_t *model)
{
    size_t slot = 0;
    while (slot < model->count && model->slots[slot].state != MODEL_UNUSED) {
        slot++;
    }
    return slot;
}

static size_t model_reclaimable(const model_t *model)
{
    size_t space = PAGE_SIZE - HEADER_SIZE - (model->count * SLOT_SIZE);
    for (size_t slot = 0; slot < model->count; slot++) {
        space -= model->slots[slot].state == MODEL_NORMAL ? model->slots[slot].length : 0;
    }
    return space;
}

static size_t model_free_space(const model_t *model)
{
    size_t cost = model_free_slot(model) == model->count ? SLOT_SIZE : 0;
    size_t space = model_reclaimable(model);
    return space > cost ? space - cost : 0;
}

static void model_insert(model_t *model, size_t length, unsigned char fill)
{
    unsigned char data[MAX_TUPLE];
    size_t slot = model_free_slot(model);
    bool fits = length <= model_free_space(model);

    memset(data, fill, length);
    errno = 0;
    int result = page_insert(&page, data, length);
    assert_int_equal(result, fits ? (int) slot : -1);
    if (!fits) {
        assert_int_equal(errno, ENOSPC);
        return;
    }
    model->slots[slot].state = MODEL_NORMAL;
    model->slots[slot].length = length;
    memcpy(model->slots[slot].data, data, length);
    model->count += slot == model->count ? 1 : 0;
}

static void model_delete(model_t *model, size_t slot)
{
    bool is_live = slot < model->count && model->slots[slot].state == MODEL_NORMAL;

    errno = 0;
    assert_int_equal(page_delete(&page, slot), is_live ? 0 : -1);
    assert_int_equal(errno, is_live ? 0 : ENOENT);
    if (is_live) {
        model->slots[slot].state = MODEL_DEAD;
    }
}

static void model_reclaim(model_t *model)
{
    size_t dead = 0;

    for (size_t slot = 0; slot < model->count; slot++) {
        dead += model->slots[slot].state == MODEL_DEAD ? 1 : 0;
        model->slots[slot].state =
            model->slots[slot].state == MODEL_DEAD ? MODEL_UNUSED : model->slots[slot].state;
    }
    assert_int_equal(page_reclaim_dead_slots(&page), dead);
}

static void model_compare_slot(const model_t *model, size_t slot)
{
    if (model->slots[slot].state == MODEL_NORMAL) {
        assert_tuple_is(slot, model->slots[slot].data, model->slots[slot].length);
    } else {
        assert_slot_is_missing(slot);
    }
}

static void model_compare(const model_t *model)
{
    assert_int_equal(page_slot_count(&page), model->count);
    assert_int_equal(page_free_space(&page), model_free_space(model));
    for (size_t slot = 0; slot < model->count; slot++) {
        model_compare_slot(model, slot);
    }
}

static void model_step(model_t *model, uint64_t *random, size_t step)
{
    size_t length = next_random(random) % (MAX_TUPLE + 1);
    size_t slot = next_random(random) % (model->count + 2);

    uint64_t action = next_random(random) % RECLAIM_ONE_IN;

    if (action < RECLAIM_ONE_IN / 2) {
        model_insert(model, length, (unsigned char) step);
    } else if (action < RECLAIM_ONE_IN - 1) {
        model_delete(model, slot);
    } else {
        model_reclaim(model);
    }
    if (step % CHECK_EVERY == 0) {
        model_compare(model);
    }
}

static void run_against_model(uint64_t seed)
{
    static model_t model;
    uint64_t random = seed;

    print_message("seed %llu\n", (unsigned long long) seed);
    page_init(&page, PAGE_TYPE_HEAP);
    model = (model_t){};
    for (size_t step = 0; step < OPERATIONS; step++) {
        model_step(&model, &random, step);
    }
    model_compare(&model);
}

static void test_page_agrees_with_a_model_under_random_operations([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261019};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        run_against_model(seeds[i]);
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
    cmocka_unit_test(test_page_agrees_with_a_model_under_random_operations),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
