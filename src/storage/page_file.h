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
 * Errors return -1 or nullptr and set errno. The errors of the system calls pass through: EIO,
 * ENOSPC, ENOMEM, ENOENT from page_file_open and EEXIST from page_file_create. These are its own.
 * ENOENT from page_file_read for a page beyond the end of the file. EBADMSG for a page that is
 * truncated, fails its checksum or fails page_validate. EBADMSG from page_file_count_pages for a
 * file that ends in the middle of a page. EIO from page_file_write for a short write.
 * page_file_read leaves the page unspecified when it fails.
 *
 * Direct I/O: a file opened with PAGE_FILE_FLAG_DIRECT bypasses the page cache of the operating
 * system (O_DIRECT), so that a page that the buffer pool caches is not cached a second time. It is
 * off unless asked for. Pages are aligned as direct I/O needs (see PAGE_ALIGNMENT in page.h), so
 * any page_t can be read and written. If the file system refuses O_DIRECT (tmpfs did, for a long
 * time), the open fails with EINVAL. The caller asked for direct I/O, so there is no silent
 * fallback to buffered I/O. Whether buffered I/O is an acceptable substitute is the caller's
 * decision. A refused page_file_create_direct may leave a new, empty file behind (the kernel
 * creates it before it checks O_DIRECT). Writes are still not durable until page_file_sync, with or
 * without direct I/O.
 */

#pragma once

#include <stddef.h>

#include "storage/page.h"

typedef struct page_file page_file_t;

/**
 * The extra_flags of page_file_open and page_file_create are optional behavior, combined with |, on
 * top of what the function always does (read and write; create without replacing). A bit that is
 * not listed here is EINVAL.
 */
constexpr unsigned PAGE_FILE_NO_EXTRA_FLAGS = 0;
constexpr unsigned PAGE_FILE_FLAG_DIRECT = 1U << 0; // direct I/O; EINVAL if the file system refuses
constexpr unsigned PAGE_FILE_FLAG_CREATE = 1U << 1; // page_file_open creates a missing file

/**
 * Opens a file for reading and writing. A file that does not exist is ENOENT, unless
 * PAGE_FILE_FLAG_CREATE is given. Then it is created in the same step, so there is no moment in
 * which another process can create it first, as there is between a failed open and a create.
 */
[[nodiscard]] page_file_t *page_file_open(const char *path, unsigned extra_flags);

/** Creates a new file; a file that already exists is EEXIST, and is not touched. */
[[nodiscard]] page_file_t *page_file_create(const char *path, unsigned extra_flags);

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
