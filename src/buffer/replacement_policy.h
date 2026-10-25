/**
 * The replacement policy of the buffer pool: which frame to reuse when a page must be brought in
 * and every frame is taken.
 *
 * A policy knows the frames by number (0 to frame_count - 1) and nothing else: not the pages in
 * them, not the pins, not the files. The pool tells it what happens to the frames and asks it for
 * a victim, so clock, LRU, FIFO and the like are interchangeable. A frame is a candidate for
 * eviction only while it is evictable, which the pool sets when the frame has no pins; a new policy
 * starts with no evictable frames.
 *
 * - record_access: the frame was used (the pool calls it at every pin).
 * - set_evictable: the frame has no pins (true) or has some (false).
 * - pick_victim: chooses an evictable frame, which stops being evictable; it fails with ENOBUFS
 *   when there is none, and the pool sets the frame evictable again if it cannot use it.
 *
 * A policy has no locks: the pool serializes the calls.
 */

#pragma once

#include <stddef.h>

typedef struct replacement_policy replacement_policy_t;

typedef struct {
    void (*destroy)(replacement_policy_t *policy);
    void (*record_access)(replacement_policy_t *policy, size_t frame);
    void (*set_evictable)(replacement_policy_t *policy, size_t frame, bool is_evictable);
    int (*pick_victim)(replacement_policy_t *policy, size_t *frame);
} replacement_policy_ops_t;

/** A policy implementation puts this first in its own struct. */
struct replacement_policy {
    const replacement_policy_ops_t *ops;
};

static inline void replacement_policy_destroy(replacement_policy_t *policy)
{
    policy->ops->destroy(policy);
}

static inline void replacement_policy_record_access(replacement_policy_t *policy, size_t frame)
{
    policy->ops->record_access(policy, frame);
}

static inline void replacement_policy_set_evictable(replacement_policy_t *policy, size_t frame,
                                                    bool is_evictable)
{
    policy->ops->set_evictable(policy, frame, is_evictable);
}

[[nodiscard]] static inline int replacement_policy_pick_victim(replacement_policy_t *policy,
                                                               size_t *frame)
{
    return policy->ops->pick_victim(policy, frame);
}
