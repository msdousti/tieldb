/**
 * Tests for the clock replacement policy
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "ds/clock_policy.h"
#include "test_random.h"

static constexpr size_t SLOTS = 4;

static int setup(void **state)
{
    *state = clock_policy_create(SLOTS);
    return *state == nullptr ? -1 : 0;
}

static int teardown(void **state)
{
    replacement_policy_destroy(*state);
    return 0;
}

static void make_evictable(replacement_policy_t *policy, size_t first, size_t last)
{
    for (size_t slot = first; slot <= last; slot++) {
        replacement_policy_set_evictable(policy, slot, true);
    }
}

static size_t victim(replacement_policy_t *policy)
{
    size_t slot = SIZE_MAX;

    assert_int_equal(replacement_policy_pick_victim(policy, &slot), 0);
    return slot;
}

static void assert_no_victim(replacement_policy_t *policy)
{
    size_t slot = 0;

    errno = 0;
    assert_int_equal(replacement_policy_pick_victim(policy, &slot), -1);
    assert_int_equal(errno, ENOBUFS);
}

static void test_a_new_policy_has_no_victim(void **state)
{
    assert_no_victim(*state);
}

static void test_an_unreferenced_evictable_slot_is_the_victim(void **state)
{
    replacement_policy_set_evictable(*state, 2, true);
    assert_int_equal(victim(*state), 2);
}

static void test_a_victim_is_not_chosen_again_until_it_is_evictable_again(void **state)
{
    replacement_policy_set_evictable(*state, 1, true);
    assert_int_equal(victim(*state), 1);
    assert_no_victim(*state);
    replacement_policy_set_evictable(*state, 1, true);
    assert_int_equal(victim(*state), 1);
}

static void test_slots_in_use_are_skipped([[maybe_unused]] void **state)
{
    replacement_policy_t *policy = *state;

    make_evictable(policy, 0, 3);
    replacement_policy_set_evictable(policy, 0, false);
    replacement_policy_set_evictable(policy, 1, false);
    assert_int_equal(victim(policy), 2);
    assert_int_equal(victim(policy), 3);
    assert_no_victim(policy);
}

static void test_a_referenced_slot_gets_a_second_chance(void **state)
{
    replacement_policy_t *policy = *state;

    make_evictable(policy, 0, 3);
    replacement_policy_record_access(policy, 0);
    assert_int_equal(victim(policy), 1);
    assert_int_equal(victim(policy), 2);
    assert_int_equal(victim(policy), 3);
    assert_int_equal(victim(policy), 0);
}

static void test_when_all_slots_are_referenced_the_hand_goes_round(void **state)
{
    replacement_policy_t *policy = *state;

    for (size_t slot = 0; slot < SLOTS; slot++) {
        replacement_policy_record_access(policy, slot);
    }
    make_evictable(policy, 0, 3);
    assert_int_equal(victim(policy), 0);
    assert_int_equal(victim(policy), 1);
}

static void test_a_policy_needs_a_slot([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(clock_policy_create(0));
    assert_int_equal(errno, EINVAL);
}

// The model is the set of evictable slots: a victim must come from it, and there is one exactly
// when the set is not empty
static void pick_and_check(replacement_policy_t *policy, bool *model)
{
    size_t count = 0;
    size_t chosen = 0;

    for (size_t slot = 0; slot < SLOTS; slot++) {
        count += model[slot] ? 1 : 0;
    }
    bool found = replacement_policy_pick_victim(policy, &chosen) == 0;
    assert_true(found == (count > 0));
    if (found) {
        assert_true(model[chosen]);
        model[chosen] = false;
    }
}

static void random_operation(replacement_policy_t *policy, bool *model, uint64_t *random)
{
    size_t slot = next_random(random) % SLOTS;
    size_t operation = next_random(random) % 3;

    if (operation == 0) {
        replacement_policy_record_access(policy, slot);
    } else if (operation == 1) {
        model[slot] = (next_random(random) & 1U) != 0;
        replacement_policy_set_evictable(policy, slot, model[slot]);
    } else {
        pick_and_check(policy, model);
    }
}

static void check_against_the_model(uint64_t seed, replacement_policy_t *policy)
{
    bool model[SLOTS] = {};
    uint64_t random = seed;

    for (int step = 0; step < 2000; step++) {
        random_operation(policy, model, &random);
    }
}

static void
test_random_operations_agree_with_the_set_of_evictable_slots([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261025};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        replacement_policy_t *policy = clock_policy_create(SLOTS);
        print_message("seed %llu\n", (unsigned long long) seeds[i]);
        check_against_the_model(seeds[i], policy);
        replacement_policy_destroy(policy);
    }
}

#define POLICY_TEST(name) cmocka_unit_test_setup_teardown(name, setup, teardown)

static const struct CMUnitTest tests[] = {
    POLICY_TEST(test_a_new_policy_has_no_victim),
    POLICY_TEST(test_an_unreferenced_evictable_slot_is_the_victim),
    POLICY_TEST(test_a_victim_is_not_chosen_again_until_it_is_evictable_again),
    POLICY_TEST(test_slots_in_use_are_skipped),
    POLICY_TEST(test_a_referenced_slot_gets_a_second_chance),
    POLICY_TEST(test_when_all_slots_are_referenced_the_hand_goes_round),
    cmocka_unit_test(test_a_policy_needs_a_slot),
    cmocka_unit_test(test_random_operations_agree_with_the_set_of_evictable_slots),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
