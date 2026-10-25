/**
 * Tests on a word of flag bits. The flags and the mask may be of any unsigned type.
 */

#pragma once

#include <stdint.h>

/** True when every bit of the mask is set in the flags. */
static inline bool flags_are_set(uint64_t flags, uint64_t mask)
{
    return (flags & mask) == mask;
}

/** True when no bit of the mask is set in the flags. */
static inline bool flags_are_clear(uint64_t flags, uint64_t mask)
{
    return (flags & mask) == 0;
}
