/**
 * The free space map: a hint of how much room each page of a heap file has, so that an insert can
 * find a page with room without reading every page.
 *
 * The map keeps one byte per heap page in the max-tree of a free space map page (fsm_page.h). The
 * byte is the free space in units of 32 bytes (a category). The tree finds the leftmost page whose
 * category is large enough, in time that depends on the height of the tree and not on the number
 * of pages. This version has one free space map page, page 0 of the map's own page file, so it
 * describes up to FSM_PAGE_LEAVES heap pages. A heap page is named by its number in its file.
 *
 * The map is a hint. Whoever inserts checks the real page, and records what it finds there. A
 * wrong answer costs a wasted look at a page, never a lost or wrong row, so the map needs no log.
 * Its pages are checked like any page, and a page 0 that fails the check is replaced by an empty
 * one, without a sync. The map is then wrong, but only in the harmless way, and the heap can
 * rebuild it.
 *
 * The rounding keeps the map safe. A page that records F free bytes gets the category F / 32,
 * rounded down, so the map never promises more than the page has. A search for N bytes looks for
 * the category N / 32 rounded up, so a page that is found has at least N free bytes if its record
 * was true. A search for 0 bytes returns page 0 of the heap, whatever it holds. A search for more
 * than FREE_SPACE_MAP_LARGEST_REQUEST finds nothing, although such a row may still fit an empty
 * page; the caller then adds a page.
 *
 * The map uses the buffer pool for its page: each call pins page 0 and unpins it before it
 * returns, and a changed page is written by the pool's own flush. The map adds the file to the
 * pool. The pool and the file stay with the caller, who closes the map before the pool and the
 * file.
 *
 * Several threads may use the map. A lock is held during each call, so a leaf and its parents
 * change together. The caller must not flush the pool while a call runs (the pool requires this for
 * any page that is changing), and must not destroy the map while another thread uses it.
 *
 * If a search finds that the inner nodes of the tree promise more than the leaves hold, it rebuilds
 * them from the leaves and searches again. Inner nodes that are too low hide a page and are not
 * seen; the heap rebuilds the whole map in a later slice.
 *
 * Errors return -1 or nullptr and set errno, as documented for each function. They also return
 * what the buffer pool and the page file return.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "buffer/buffer_pool.h"
#include "storage/page_file.h"

typedef struct free_space_map free_space_map_t;

// The bytes of free space that one category stands for
constexpr size_t FREE_SPACE_MAP_CATEGORY_SIZE = 32;

// The largest free space that a search can ask for: the largest category (a byte), in bytes
constexpr size_t FREE_SPACE_MAP_LARGEST_REQUEST = UINT8_MAX * FREE_SPACE_MAP_CATEGORY_SIZE;

/**
 * Opens the map in file, which is empty or holds a map. An empty file gets its page 0. A page 0
 * that is damaged (a torn write, for instance) is replaced by an empty one, without a sync: the map
 * is only a hint. The map is added to pool. EBADMSG: page 0 is a valid page of another type, or the
 * file ends in the middle of a page. ENOMEM.
 */
[[nodiscard]] free_space_map_t *free_space_map_create(buffer_pool_t *pool, page_file_t *file);

/** Frees the map. Nothing is written: the pool flushes the page. */
void free_space_map_destroy(free_space_map_t *map);

/**
 * Says that heap_page has free_bytes of free space, which replaces what the map said before. The
 * page 0 of the map is marked changed in the pool.
 * ENOSPC: heap_page is FSM_PAGE_LEAVES or more. EINVAL: free_bytes is larger than a page body.
 * EBADMSG: page 0 is no longer a free space map page.
 */
[[nodiscard]] int free_space_map_record(free_space_map_t *map, size_t heap_page, size_t free_bytes);

/**
 * Puts the number of the first heap page that has at least needed_bytes of free space, according
 * to the map, into *heap_page. ENOENT: the map knows no such page (see the rounding above).
 * EBADMSG: page 0 is no longer a free space map page.
 */
[[nodiscard]] int free_space_map_find(free_space_map_t *map, size_t needed_bytes,
                                      size_t *heap_page);
