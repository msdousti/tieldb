/**
 * A replacement policy: which slot of a fixed set to reuse when something new must be cached and
 * every slot is taken. The buffer pool uses it for its page frames; a cache of open files or of
 * commit status pages could use it in the same way, with a hash map from keys to slot numbers.
 *
 * A policy knows the slots by number (0 to slot_count - 1) and nothing else: not what they hold,
 * not who uses them. The user tells it what happens to the slots and asks it for a victim, so
 * clock, LRU, FIFO and the like are interchangeable. A slot is a candidate for eviction only while
 * it is evictable (the buffer pool sets it when a frame has no pins); a new policy starts with no
 * evictable slots, and every slot weighs the same.
 *
 * - record_access: the slot was used (the buffer pool calls it at every pin).
 * - set_evictable: the slot may be reused (true) or is in use (false).
 * - pick_victim: chooses an evictable slot, which stops being evictable; it fails with ENOBUFS
 *   when there is none, and the user sets the slot evictable again if it cannot use it.
 *
 * A policy has no locks: the user serializes the calls.
 */

#pragma once

#include <stddef.h>

typedef struct replacement_policy replacement_policy_t;

typedef struct {
    void (*destroy)(replacement_policy_t *policy);
    void (*record_access)(replacement_policy_t *policy, size_t slot);
    void (*set_evictable)(replacement_policy_t *policy, size_t slot, bool is_evictable);
    int (*pick_victim)(replacement_policy_t *policy, size_t *slot);
} replacement_policy_ops_t;

/** A policy implementation puts this first in its own struct. */
struct replacement_policy {
    const replacement_policy_ops_t *ops;
};

static inline void replacement_policy_destroy(replacement_policy_t *policy)
{
    policy->ops->destroy(policy);
}

static inline void replacement_policy_record_access(replacement_policy_t *policy, size_t slot)
{
    policy->ops->record_access(policy, slot);
}

static inline void replacement_policy_set_evictable(replacement_policy_t *policy, size_t slot,
                                                    bool is_evictable)
{
    policy->ops->set_evictable(policy, slot, is_evictable);
}

[[nodiscard]] static inline int replacement_policy_pick_victim(replacement_policy_t *policy,
                                                               size_t *slot)
{
    return policy->ops->pick_victim(policy, slot);
}
