/**
 * Slotted pages: the pages in which tuples are stored.
 *
 * A slotted page is a page_t with a fixed header, then an array of slots that grows upward, and the
 * tuples, which grow downward from the end of the page. Each slot holds the offset and length of
 * one tuple, so a tuple is named by its slot number, and the number stays the same when the page
 * moves tuples around:
 *
 *   | header | slot 0 | slot 1 | ...      free space      ... | tuple 1 | tuple 0 |
 *
 * The page does not look inside a tuple; it stores and returns bytes.
 *
 * Deleting a tuple frees its space at once but not its slot number. The slot becomes dead, so
 * nothing that still holds the number (an index entry, a reader) can see a new tuple under it.
 * slotted_page_reclaim_dead_slots makes dead slots unused. Only unused numbers are given to new
 * tuples (the lowest first). The storage engine calls it when no one can still refer to a dead
 * slot, which for an engine without indexes or concurrent readers is right after deleting. When
 * the free space is in pieces, slotted_page_insert first moves the tuples together.
 *
 * A slotted_page_t is a view of a page_t of a slotted type (heap or catalog). Other page types
 * use the bytes after the header in their own way, so their pages cannot be opened as slotted.
 * The view holds no resources and needs no destroy function. The page stays with the caller, who
 * keeps it alive while the view is used. Opening a page checks only that it has the expected kind.
 * Pages read from disk are checked by page_validate first.
 *
 * Slot numbers and tuple lengths must come from the page itself. The functions do no locking.
 *
 * Errors return -1 or nullptr and set errno: ENOSPC when a tuple does not fit in the page, ENOENT
 * for a slot that does not exist or was deleted, EINVAL for a page of the wrong type.
 */

#pragma once

#include <stddef.h>

#include "storage/page.h"

/** The state of a slot. A new slot is normal; deleting its tuple makes it dead. */
typedef enum {
    SLOT_UNUSED = 0, // no tuple, and the number may be given to a new tuple
    SLOT_NORMAL = 1, // holds a tuple
    SLOT_DEAD = 2,   // the tuple is deleted; the number stays taken until page_reclaim_dead_slots
} slot_state_t;

/** The page types that are slotted. The values equal the page types. */
typedef enum {
    SLOTTED_PAGE_HEAP = PAGE_TYPE_HEAP,
    SLOTTED_PAGE_CATALOG = PAGE_TYPE_CATALOG,
} slotted_page_kind_t;

typedef struct {
    page_t *page;
} slotted_page_t;

/** Whether pages of this type are slotted. */
[[nodiscard]] bool slotted_page_is_type(page_type_t type);

/** Makes slotted a view of page. EINVAL: the page is not of the kind. */
[[nodiscard]] int slotted_page_open(slotted_page_t *slotted, page_t *page,
                                    slotted_page_kind_t kind);

/** Turns page into an empty page of the kind and makes slotted a view of it. */
void slotted_page_init(slotted_page_t *slotted, page_t *page, slotted_page_kind_t kind);

/** Stores a copy of the tuple and returns its slot number. */
[[nodiscard]] int slotted_page_insert(slotted_page_t *slotted, const void *tuple, size_t length);

/** The tuple in a slot, with its length in *length; valid until the next change to the page. */
[[nodiscard]] const void *slotted_page_get(const slotted_page_t *slotted, size_t slot,
                                           size_t *length);

[[nodiscard]] int slotted_page_delete(slotted_page_t *slotted, size_t slot);

/** Makes dead slots reusable; returns how many there were. */
size_t slotted_page_reclaim_dead_slots(slotted_page_t *slotted);

/** The number of slots, dead and unused ones included; slot numbers below it may be valid. */
[[nodiscard]] size_t slotted_page_slot_count(const slotted_page_t *slotted);

/**
 * The length of the longest tuple that slotted_page_insert would accept now. 0 means that no tuple
 * with a byte of data fits; an empty one may still fit.
 */
[[nodiscard]] size_t slotted_page_free_space(const slotted_page_t *slotted);

/**
 * Checks that the slots of a page of a slotted type are consistent, so that the functions above
 * can trust the page. Called by page_validate. EBADMSG: the page is corrupt.
 */
[[nodiscard]] int slotted_page_validate(const page_t *page);
