/**
 * The clock replacement policy: an approximation of LRU that costs one bit per slot.
 *
 * Each slot has a reference bit, set when the slot is used. A hand sweeps over the slots: it
 * skips the slots that are not evictable, clears the reference bit of the ones that are
 * referenced (a second chance), and picks the first evictable slot whose bit is clear. The hand
 * continues from where it stopped.
 *
 * Errors return nullptr and set errno: ENOMEM, and EINVAL for no slots.
 */

#pragma once

#include "ds/replacement_policy.h"

/** A policy for slot_count slots, none of them evictable. */
[[nodiscard]] replacement_policy_t *clock_policy_create(size_t slot_count);
