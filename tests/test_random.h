/**
 * A small deterministic random number generator for the reference-model tests.
 *
 * rand() is not used because its sequence differs between C libraries, so a logged seed would not
 * reproduce a failure on another machine. The seed must not be zero.
 */

#pragma once

#include <stdint.h>

static inline uint64_t next_random(uint64_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}
