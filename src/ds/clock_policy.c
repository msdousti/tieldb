#include "ds/clock_policy.h"

#include <errno.h>
#include <stdlib.h>

typedef struct {
    bool is_evictable;
    bool is_referenced;
} clock_slot_t;

typedef struct {
    replacement_policy_t base;
    size_t slot_count;
    size_t hand;
    clock_slot_t slots[];
} clock_policy_t;

static void clock_destroy(replacement_policy_t *policy)
{
    free(policy);
}

static void clock_record_access(replacement_policy_t *policy, size_t slot)
{
    ((clock_policy_t *) policy)->slots[slot].is_referenced = true;
}

static void clock_set_evictable(replacement_policy_t *policy, size_t slot, bool is_evictable)
{
    ((clock_policy_t *) policy)->slots[slot].is_evictable = is_evictable;
}

static bool take_if_victim(clock_slot_t *candidate)
{
    if (!candidate->is_evictable) {
        return false;
    }
    if (candidate->is_referenced) {
        candidate->is_referenced = false;
        return false;
    }
    candidate->is_evictable = false;
    return true;
}

static size_t advance_hand(clock_policy_t *clock)
{
    size_t index = clock->hand;
    clock->hand = (clock->hand + 1) % clock->slot_count;
    return index;
}

// Two rounds are enough: the first one clears every reference bit that is in the way
static int clock_pick_victim(replacement_policy_t *policy, size_t *slot)
{
    clock_policy_t *clock = (clock_policy_t *) policy;
    size_t index = 0;
    bool found = false;
    for (size_t step = 0; !found && step < 2 * clock->slot_count; step++) {
        index = advance_hand(clock);
        found = take_if_victim(&clock->slots[index]);
    }
    if (!found) {
        errno = ENOBUFS;
        return -1;
    }
    *slot = index;
    return 0;
}

static const replacement_policy_ops_t CLOCK_OPS = {
    .destroy = clock_destroy,
    .record_access = clock_record_access,
    .set_evictable = clock_set_evictable,
    .pick_victim = clock_pick_victim,
};

replacement_policy_t *clock_policy_create(size_t slot_count)
{
    if (slot_count == 0) {
        errno = EINVAL;
        return nullptr;
    }
    clock_policy_t *clock = calloc(1, sizeof(*clock) + (slot_count * sizeof(clock->slots[0])));
    if (clock == nullptr) {
        return nullptr;
    }
    clock->base.ops = &CLOCK_OPS;
    clock->slot_count = slot_count;
    return &clock->base;
}
