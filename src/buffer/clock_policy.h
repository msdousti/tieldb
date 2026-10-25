/**
 * The clock replacement policy: an approximation of LRU that costs one bit per frame.
 *
 * Each frame has a reference bit, set when the frame is used. A hand sweeps over the frames: it
 * skips the frames that are not evictable, clears the reference bit of the ones that are
 * referenced (a second chance), and picks the first evictable frame whose bit is clear. The hand
 * continues from where it stopped.
 *
 * Errors return nullptr and set errno: ENOMEM, and EINVAL for no frames.
 */

#pragma once

#include "buffer/replacement_policy.h"

/** A policy for frame_count frames, none of them evictable. */
[[nodiscard]] replacement_policy_t *clock_policy_create(size_t frame_count);
