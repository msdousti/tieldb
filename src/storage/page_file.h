/**
 * A file of pages: page n of the file is the bytes from n * PAGE_SIZE on.
 *
 * All I/O goes through io->, so tests can inject faults. A page is checked when it is read: its
 * CRC-32C must match and page_validate must accept it (see page.h), so the caller can trust the
 * pages that page_file_read returns. The checksum is set when a page is written, on a copy, so the
 * caller's page is not changed. Writing a page does not make it durable: call page_file_sync.
 *
 * The file has as many pages as its size says (page_file_count_pages); reading past the end is
 * ENOENT. A page that was never written (a gap in the file, which reads as zeros) is corrupt, not
 * empty. One thread at a time uses a page_file_t.
 *
 * Errors return -1 or nullptr and set errno, as the system calls do (EIO, ENOSPC, ENOENT from
 * page_file_open, EEXIST from page_file_create, ENOMEM), with these of its own: ENOENT from
 * page_file_read for a page beyond the end of the file; EBADMSG for a page that is truncated, fails
 * its checksum or fails page_validate, and from page_file_count_pages for a file that ends in the
 * middle of a page; EIO from page_file_write for a short write. page_file_read
 * leaves the page unspecified when it fails.
 *
 * Direct I/O: a file opened with the _direct functions bypasses the page cache of the operating
 * system (O_DIRECT), so that a page that the buffer pool caches is not cached a second time. It is
 * off unless asked for. Pages are aligned as direct I/O needs (see PAGE_ALIGNMENT in page.h), so
 * any page_t can be read and written. If the file system refuses O_DIRECT (tmpfs did, for a long
 * time), the file is opened without it and page_file_is_direct says so. Writes are still not
 * durable until page_file_sync, with or without direct I/O.
 */

#pragma once

#include <stddef.h>

#include "storage/page.h"

typedef struct page_file page_file_t;

/** Opens an existing file for reading and writing; a file that does not exist is ENOENT. */
[[nodiscard]] page_file_t *page_file_open(const char *path);

/** Creates a new file; a file that already exists is EEXIST, and is not touched. */
[[nodiscard]] page_file_t *page_file_create(const char *path);

/** Like page_file_open, with direct I/O if the file system allows it. */
[[nodiscard]] page_file_t *page_file_open_direct(const char *path);

/** Like page_file_create, with direct I/O if the file system allows it. */
[[nodiscard]] page_file_t *page_file_create_direct(const char *path);

/** True if the file really uses direct I/O. */
[[nodiscard]] bool page_file_is_direct(const page_file_t *file);

/** Closes the file and frees the handle, also when closing fails. */
[[nodiscard]] int page_file_close(page_file_t *file);

[[nodiscard]] int page_file_read(page_file_t *file, size_t page_number, page_t *page);

[[nodiscard]] int page_file_write(page_file_t *file, size_t page_number, const page_t *page);

[[nodiscard]] int page_file_sync(page_file_t *file);

/** The number of pages in the file, from its size: the next page to add is this number. */
[[nodiscard]] int page_file_count_pages(page_file_t *file, size_t *count);

/**
 * Adds an empty page of the given type at the end of the file and returns its number. The page is
 * written, but not made durable (see page_file_sync). If the write is torn, the file ends in the
 * middle of a page and counting its pages fails with EBADMSG until the file is repaired.
 */
[[nodiscard]] int page_file_allocate_page(page_file_t *file, page_type_t type, size_t *page_number);
