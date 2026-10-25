/**
 * The buffer pool: a fixed number of in-memory copies of pages (frames) of page files.
 *
 * The pool knows nothing about what the pages hold, so the heap engine and any other engine can
 * use it. A page file is added to the pool once and gets a file number; a page is then named by
 * its page_id_t, the file number and the page number in that file. buffer_pool_pin brings the page
 * into a frame (reading and checking it, see page_file.h) and returns a pointer to it; the pointer
 * stays valid until the matching buffer_pool_unpin. A page that is pinned is never replaced, and
 * every pin must be unpinned: buffer_pool_destroy reports the ones that were not.
 *
 * The pool cannot see that the caller writes to a page, so the caller says so with
 * buffer_pool_mark_changed, while the page is pinned and when it changes it. A marked page is
 * written back to its file by buffer_pool_flush_all, which then syncs the files; flushing does not
 * wait for pinned pages, so the caller makes sure that nobody is changing a page while it is
 * flushed. The pool does not write a page by itself.
 *
 * The pool has no locks yet: one thread at a time uses it. It does not own the page files: they
 * stay open until the pool is destroyed, and the caller closes them.
 *
 * Errors return -1 or nullptr and set errno: ENOMEM; EINVAL for a file number that was not added
 * or a page that is not pinned; ENOBUFS when every frame is in use; EBUSY from buffer_pool_destroy
 * when pages are still pinned; and what page_file_read, page_file_write and page_file_sync return.
 */

#pragma once

#include <stddef.h>

#include "storage/page_file.h"

typedef struct buffer_pool buffer_pool_t;

typedef struct {
    size_t file_number;
    size_t page_number;
} page_id_t;

/** A pool of frame_count frames. */
[[nodiscard]] buffer_pool_t *buffer_pool_create(size_t frame_count);

/**
 * Frees the pool, whatever its state: pages that were changed and not flushed are lost. Fails
 * with EBUSY (after freeing) if a page is still pinned: a missing unpin.
 */
[[nodiscard]] int buffer_pool_destroy(buffer_pool_t *pool);

/** Adds a page file and returns its file number. */
[[nodiscard]] int buffer_pool_add_file(buffer_pool_t *pool, page_file_t *file, size_t *file_number);

/** Pins the page, reading it from its file if it is not in the pool. */
[[nodiscard]] int buffer_pool_pin(buffer_pool_t *pool, page_id_t id, page_t **page);

[[nodiscard]] int buffer_pool_unpin(buffer_pool_t *pool, page_id_t id);

/** Says that the caller changed the page, which must be pinned; flush_all will write it. */
[[nodiscard]] int buffer_pool_mark_changed(buffer_pool_t *pool, page_id_t id);

/** Writes the changed pages, then syncs the files. A page whose write failed stays changed. */
[[nodiscard]] int buffer_pool_flush_all(buffer_pool_t *pool);
