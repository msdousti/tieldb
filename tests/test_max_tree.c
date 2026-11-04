/**
 * Tests for the binary max-tree
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "ds/max_tree.h"
#include "test_random.h"

static constexpr size_t MAX_LEAVES = 300;
static constexpr size_t MAX_BYTES = 1024;

typedef struct {
    max_tree_t tree;
    uint8_t memory[MAX_BYTES];
} fixture_t;

static void make_tree(fixture_t *fixture, size_t leaf_count)
{
    assert_int_equal(
        max_tree_init(&fixture->tree, fixture->memory, sizeof(fixture->memory), leaf_count), 0);
}

static size_t size_for(size_t leaf_count)
{
    size_t bytes = 0;

    assert_int_equal(max_tree_size(leaf_count, &bytes), 0);
    return bytes;
}

static void test_the_size_is_a_padded_binary_tree(void **state)
{
    (void) state;
    assert_int_equal(size_for(1), 1);
    assert_int_equal(size_for(2), 3);
    assert_int_equal(size_for(3), 7);
    assert_int_equal(size_for(4), 7);
    assert_int_equal(size_for(5), 15);
    assert_int_equal(size_for(300), 1023);
}

static void test_the_size_rejects_no_leaves_and_overflow(void **state)
{
    size_t bytes = 0;
    constexpr size_t half = SIZE_MAX / 2 + 1;

    (void) state;
    errno = 0;
    assert_int_equal(max_tree_size(0, &bytes), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(max_tree_size(half, &bytes), -1);
    assert_int_equal(errno, EOVERFLOW);
    errno = 0;
    assert_int_equal(max_tree_size(SIZE_MAX, &bytes), -1);
    assert_int_equal(errno, EOVERFLOW);
}

static void test_init_checks_its_arguments(void **state)
{
    fixture_t fixture;
    max_tree_t tree;

    (void) state;
    errno = 0;
    assert_int_equal(max_tree_init(&tree, fixture.memory, sizeof(fixture.memory), 0), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(max_tree_init(&tree, nullptr, sizeof(fixture.memory), 4), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(max_tree_init(&tree, fixture.memory, 6, 4), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(max_tree_init(&tree, fixture.memory, sizeof(fixture.memory), SIZE_MAX), -1);
    assert_int_equal(errno, EOVERFLOW);
    assert_int_equal(max_tree_init(&tree, fixture.memory, 7, 4), 0);
}

static void test_a_new_tree_holds_zeros(void **state)
{
    fixture_t fixture;
    uint8_t value = 1;
    size_t leaf = SIZE_MAX;

    (void) state;
    memset(fixture.memory, 0xAA, sizeof(fixture.memory));
    make_tree(&fixture, 5);
    assert_int_equal(max_tree_root(&fixture.tree), 0);
    assert_int_equal(max_tree_get(&fixture.tree, 4, &value), 0);
    assert_int_equal(value, 0);
    assert_int_equal(max_tree_find(&fixture.tree, 0, &leaf), 0);
    assert_int_equal(leaf, 0);
    errno = 0;
    assert_int_equal(max_tree_find(&fixture.tree, 1, &leaf), -1);
    assert_int_equal(errno, ENOENT);
}

static void test_a_tree_of_one_leaf_is_its_root(void **state)
{
    fixture_t fixture;
    size_t leaf = SIZE_MAX;

    (void) state;
    make_tree(&fixture, 1);
    assert_int_equal(max_tree_set(&fixture.tree, 0, 42), 0);
    assert_int_equal(max_tree_root(&fixture.tree), 42);
    assert_int_equal(max_tree_find(&fixture.tree, 42, &leaf), 0);
    assert_int_equal(leaf, 0);
}

static void test_a_leaf_that_does_not_exist_is_rejected(void **state)
{
    fixture_t fixture;
    uint8_t value = 0;

    (void) state;
    make_tree(&fixture, 5);
    errno = 0;
    assert_int_equal(max_tree_set(&fixture.tree, 5, 1), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(max_tree_get(&fixture.tree, 5, &value), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(max_tree_root(&fixture.tree), 0);
}

static void test_find_returns_the_leftmost_leaf_that_is_large_enough(void **state)
{
    fixture_t fixture;
    size_t leaf = SIZE_MAX;
    const uint8_t values[] = {10, 0, 45, 3, 0, 0, 7, 120};

    (void) state;
    make_tree(&fixture, 8);
    for (size_t i = 0; i < sizeof(values); i++) {
        assert_int_equal(max_tree_set(&fixture.tree, i, values[i]), 0);
    }
    assert_int_equal(max_tree_root(&fixture.tree), 120);
    assert_int_equal(max_tree_find(&fixture.tree, 10, &leaf), 0);
    assert_int_equal(leaf, 0);
    assert_int_equal(max_tree_find(&fixture.tree, 11, &leaf), 0);
    assert_int_equal(leaf, 2);
    assert_int_equal(max_tree_find(&fixture.tree, 50, &leaf), 0);
    assert_int_equal(leaf, 7);
}

static void test_lowering_a_leaf_lowers_the_root(void **state)
{
    fixture_t fixture;

    (void) state;
    make_tree(&fixture, 4);
    assert_int_equal(max_tree_set(&fixture.tree, 1, 9), 0);
    assert_int_equal(max_tree_set(&fixture.tree, 3, 5), 0);
    assert_int_equal(max_tree_root(&fixture.tree), 9);
    assert_int_equal(max_tree_set(&fixture.tree, 1, 2), 0);
    assert_int_equal(max_tree_root(&fixture.tree), 5);
    assert_int_equal(max_tree_set(&fixture.tree, 3, 0), 0);
    assert_int_equal(max_tree_root(&fixture.tree), 2);
}

static uint8_t model_maximum(const uint8_t *model, size_t leaf_count)
{
    uint8_t maximum = 0;

    for (size_t i = 0; i < leaf_count; i++) {
        maximum = model[i] > maximum ? model[i] : maximum;
    }
    return maximum;
}

static size_t model_find(const uint8_t *model, size_t leaf_count, uint8_t at_least)
{
    size_t leaf = 0;

    while (leaf < leaf_count && model[leaf] < at_least) {
        leaf++;
    }
    return leaf < leaf_count ? leaf : SIZE_MAX;
}

static void check_find(const max_tree_t *tree, const uint8_t *model, uint8_t at_least)
{
    size_t expected = model_find(model, tree->leaf_count, at_least);
    size_t found = SIZE_MAX;

    errno = 0;
    assert_int_equal(max_tree_find(tree, at_least, &found), expected == SIZE_MAX ? -1 : 0);
    assert_int_equal(found, expected);
    assert_int_equal(errno, expected == SIZE_MAX ? ENOENT : 0);
}

static void check_against_the_model(uint64_t seed, size_t leaf_count)
{
    fixture_t fixture;
    uint8_t model[MAX_LEAVES] = {};
    uint64_t random = seed;

    make_tree(&fixture, leaf_count);
    for (int step = 0; step < 2000; step++) {
        size_t leaf = next_random(&random) % leaf_count;
        uint8_t value = (uint8_t) (next_random(&random) % 4 == 0 ? 0 : next_random(&random));
        uint8_t read_back = 0;

        model[leaf] = value;
        assert_int_equal(max_tree_set(&fixture.tree, leaf, value), 0);
        assert_int_equal(max_tree_root(&fixture.tree), model_maximum(model, leaf_count));
        assert_int_equal(max_tree_get(&fixture.tree, leaf, &read_back), 0);
        assert_int_equal(read_back, value);
        check_find(&fixture.tree, model, (uint8_t) next_random(&random));
    }
}

static void test_random_updates_agree_with_a_plain_array(void **state)
{
    const size_t counts[] = {1, 2, 3, 5, 7, 8, 9, 100, 255, 256, 257, MAX_LEAVES};

    (void) state;
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        print_message("leaves %zu\n", counts[i]);
        check_against_the_model(0x9E3779B97F4A7C15ULL + i, counts[i]);
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_the_size_is_a_padded_binary_tree),
    cmocka_unit_test(test_the_size_rejects_no_leaves_and_overflow),
    cmocka_unit_test(test_init_checks_its_arguments),
    cmocka_unit_test(test_a_new_tree_holds_zeros),
    cmocka_unit_test(test_a_tree_of_one_leaf_is_its_root),
    cmocka_unit_test(test_a_leaf_that_does_not_exist_is_rejected),
    cmocka_unit_test(test_find_returns_the_leftmost_leaf_that_is_large_enough),
    cmocka_unit_test(test_lowering_a_leaf_lowers_the_root),
    cmocka_unit_test(test_random_updates_agree_with_a_plain_array),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
