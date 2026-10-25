/**
 * A run of bytes that someone else owns: a pointer and its length, kept together. An empty span
 * may have a null pointer.
 */

#pragma once

#include <stddef.h>

typedef struct {
    const void *data;
    size_t length;
} byte_span_t;
