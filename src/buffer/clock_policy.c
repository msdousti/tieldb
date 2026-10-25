#include "buffer/clock_policy.h"

#include <errno.h>
#include <stdlib.h>

typedef struct {
    bool is_evictable;
    bool is_referenced;
} clock_frame_t;

typedef struct {
    replacement_policy_t base;
    size_t frame_count;
    size_t hand;
    clock_frame_t frames[];
} clock_policy_t;

static void clock_destroy(replacement_policy_t *policy)
{
    free(policy);
}

static void clock_record_access(replacement_policy_t *policy, size_t frame)
{
    ((clock_policy_t *) policy)->frames[frame].is_referenced = true;
}

static void clock_set_evictable(replacement_policy_t *policy, size_t frame, bool is_evictable)
{
    ((clock_policy_t *) policy)->frames[frame].is_evictable = is_evictable;
}

static bool take_if_victim(clock_frame_t *candidate)
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
    clock->hand = (clock->hand + 1) % clock->frame_count;
    return index;
}

// Two rounds are enough: the first one clears every reference bit that is in the way
static int clock_pick_victim(replacement_policy_t *policy, size_t *frame)
{
    clock_policy_t *clock = (clock_policy_t *) policy;
    size_t index = 0;
    bool found = false;
    for (size_t step = 0; !found && step < 2 * clock->frame_count; step++) {
        index = advance_hand(clock);
        found = take_if_victim(&clock->frames[index]);
    }
    if (!found) {
        errno = ENOBUFS;
        return -1;
    }
    *frame = index;
    return 0;
}

static const replacement_policy_ops_t CLOCK_OPS = {
    .destroy = clock_destroy,
    .record_access = clock_record_access,
    .set_evictable = clock_set_evictable,
    .pick_victim = clock_pick_victim,
};

replacement_policy_t *clock_policy_create(size_t frame_count)
{
    if (frame_count == 0) {
        errno = EINVAL;
        return nullptr;
    }
    clock_policy_t *clock = calloc(1, sizeof(*clock) + (frame_count * sizeof(clock->frames[0])));
    if (clock == nullptr) {
        return nullptr;
    }
    clock->base.ops = &CLOCK_OPS;
    clock->frame_count = frame_count;
    return &clock->base;
}
