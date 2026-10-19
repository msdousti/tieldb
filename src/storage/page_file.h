/**
 * A file of pages: page n of the file is the bytes from n * PAGE_SIZE on.
 *
 * All I/O goes through io->, so tests can inject faults. A page is checked when it is read: its
 * CRC-32C must match and page_validate must accept it (see page.h), so the caller can trust the
 * pages that page_file_read returns. The checksum is set when a page is written, on a copy, so the
 * caller's page is not changed. Writing a page does not make it durable: call page_file_sync.
 *
 * The file does not know how many pages it has; reading past the end is ENOENT. A page that was
 * never written (a gap in the file, which reads as zeros) is corrupt, not empty. One thread at a
 * time uses a page_file_t.
 *
 * Errors return -1 or nullptr and set errno, as the system calls do (EIO, ENOSPC, ENOENT from
 * page_file_open, EEXIST from page_file_create, ENOMEM), with these of its own: ENOENT from
 * page_file_read for a page beyond the end of the file; EBADMSG for a page that is truncated, fails
 * its checksum or fails page_validate; EIO from page_file_write for a short write. page_file_read
 * leaves the page unspecified when it fails.
 */

#pragma once

#include <stddef.h>

#include "storage/page.h"

typedef struct page_file page_file_t;

/** Opens an existing file for reading and writing; a file that does not exist is ENOENT. */
[[nodiscard]] page_file_t *page_file_open(const char *path);

/** Creates a new file; a file that already exists is EEXIST, and is not touched. */
[[nodiscard]] page_file_t *page_file_create(const char *path);

/** Closes the file and frees the handle, also when closing fails. */
[[nodiscard]] int page_file_close(page_file_t *file);

[[nodiscard]] int page_file_read(page_file_t *file, size_t page_number, page_t *page);

[[nodiscard]] int page_file_write(page_file_t *file, size_t page_number, const page_t *page);

[[nodiscard]] int page_file_sync(page_file_t *file);
