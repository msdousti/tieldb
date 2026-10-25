/**
 * The heap tuple: a row as the heap engine stores it in a page, with a header in front of it.
 *
 * A tuple is a 16-bit flags word, then the transaction IDs xmin and xmax (64 bits each) when
 * HEAP_TUPLE_HAS_XIDS is set, then the row as the row codec laid it out (see row_codec.h). The
 * IDs are there for the multi-version concurrency control of Phase 5; until then no tuple has
 * them, so a tuple costs two bytes more than its row. The flags that are not named here are
 * reserved and must be zero. Tuples are encoded and decoded in the machine's byte order, like the
 * rows.
 *
 * heap_tuple_decode returns a row that points into the bytes it was given. The header says nothing
 * about the length of the row: the length of the tuple, which the page keeps, gives it.
 *
 * A tuple ID (TID) says where a tuple is: the page of its file and the slot in that page. It is
 * private to the heap engine; the executor sees rows and cursors only.
 *
 * Errors return -1 and set errno: EINVAL for a reserved flag in a tuple to encode, E2BIG when the
 * tuple does not fit the buffer of heap_tuple_encode, EBADMSG for bytes that are not a tuple (too
 * short, or with a reserved flag).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "common/byte_span.h"

constexpr uint16_t HEAP_TUPLE_HAS_XIDS = 1U << 0;

typedef struct {
    uint16_t flags;
    uint64_t xmin; // 0 without HEAP_TUPLE_HAS_XIDS
    uint64_t xmax; // 0 without HEAP_TUPLE_HAS_XIDS
    byte_span_t row;
} heap_tuple_t;

typedef struct {
    size_t page_number;
    uint16_t slot;
} heap_tid_t;

/** The size of the tuple as heap_tuple_encode writes it, not of the struct in memory. */
[[nodiscard]] ssize_t heap_tuple_encoded_size(const heap_tuple_t *tuple);

/** Writes the tuple; returns its size. */
[[nodiscard]] ssize_t heap_tuple_encode(const heap_tuple_t *tuple, void *buffer, size_t capacity);

/** Reads a tuple of exactly length bytes. */
[[nodiscard]] int heap_tuple_decode(const void *bytes, size_t length, heap_tuple_t *tuple);
