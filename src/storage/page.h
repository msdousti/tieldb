/**
 * Slotted pages: the 8 KB unit in which tuples are stored on disk.
 *
 * A page is a byte array with a fixed header, then an array of slots that grows upward, and the
 * tuples, which grow downward from the end of the page. Each slot holds the offset and length of
 * one tuple, so a tuple is named by its slot number, and the number stays the same when the page
 * moves tuples around:
 *
 *   | header | slot 0 | slot 1 | ...      free space      ... | tuple 1 | tuple 0 |
 *
 * The header holds the page type, the LSN of the last change (zero until the WAL exists), a
 * checksum field (filled in when the page is written to disk) and the bookkeeping for the slots.
 * The page does not look inside a tuple; it stores and returns bytes.
 *
 * Deleting a tuple frees its slot number for reuse: page_insert gives a new tuple the lowest free
 * slot number, or a new one at the end if there is none. The space of a deleted tuple is reused
 * too: when the free space is in pieces, page_insert first moves the tuples together.
 *
 * Pages have no locks; the buffer pool decides who may use a page. Slot numbers and tuple
 * lengths must come from the page itself.
 *
 * Errors return -1 or nullptr and set errno: ENOSPC when a tuple does not fit in the page, ENOENT
 * for a slot that does not exist or was deleted.
 */

#pragma once

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t PAGE_SIZE = 8192;

typedef enum {
    PAGE_TYPE_FREE = 0,
    PAGE_TYPE_HEAP = 1,
} page_type_t;

typedef struct {
    alignas(uint64_t) unsigned char bytes[PAGE_SIZE];
} page_t;

/** Turns the page into an empty page of the given type. */
void page_init(page_t *page, page_type_t type);

[[nodiscard]] page_type_t page_type(const page_t *page);

/** Stores a copy of the tuple and returns its slot number. */
[[nodiscard]] int page_insert(page_t *page, const void *tuple, size_t length);

/** The tuple in a slot, with its length in *length; valid until the next change to the page. */
[[nodiscard]] const void *page_get(const page_t *page, size_t slot, size_t *length);

[[nodiscard]] int page_delete(page_t *page, size_t slot);

/** The number of slots, deleted ones included; slot numbers below it may be valid. */
[[nodiscard]] size_t page_slot_count(const page_t *page);

/** The length of the longest tuple that page_insert would accept now. */
[[nodiscard]] size_t page_free_space(const page_t *page);
