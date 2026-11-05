/**
 * Pages: the 8 KB unit in which data is stored on disk.
 *
 * Every page starts with the same header. It holds the page type, the LSN of the last change (zero
 * until the WAL exists), a checksum field (filled in when the page is written to disk) and
 * bookkeeping that the page type uses. The bytes after the header belong to the page type. The
 * slotted pages of the heap and the catalog are in slotted_page.h, and each type has its own
 * module with its own functions. This module has what all pages share: the type, the flags, the
 * checksum and the validation.
 *
 * Each page has 16 flag bits for the storage engine; the page stores them and does not look at
 * them. Flags of a tuple belong in the tuple's own header.
 *
 * Pages have no locks; the buffer pool decides who may use a page.
 *
 * A page read from disk is checked before it is used. page_verify_checksum compares the CRC-32C in
 * the header with the page. page_validate checks that the header is consistent and, for a slotted
 * page, the slots too, so that the functions of the page type can trust the page. A page of zeros
 * is not valid: use page_init. page_set_checksum fills in the checksum, just before a page is
 * written.
 *
 * Errors return -1 or nullptr and set errno: EBADMSG for a page that is corrupt.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t PAGE_SIZE = 8192;

// The alignment of a page in memory: direct I/O (O_DIRECT) needs buffers aligned to the logical
// block size of the device, up to 4096 bytes. It is part of the type, so that a page can always be
// a buffer
constexpr size_t PAGE_ALIGNMENT = 4096;

typedef enum {
    PAGE_TYPE_FREE = 0,
    PAGE_TYPE_HEAP = 1,
    PAGE_TYPE_CATALOG = 2,
    PAGE_TYPE_FSM = 3,
} page_type_t;

typedef struct {
    alignas(PAGE_ALIGNMENT) unsigned char bytes[PAGE_SIZE];
} page_t;

static_assert(sizeof(page_t) == PAGE_SIZE);

/**
 * Memory for count pages, not initialized, aligned as page_t requires (malloc does not align that
 * much); free() releases it. Not zeroing keeps a large array lazy: the operating system gives the
 * pages only when they are used. A page_t in heap memory must come from here or sit in a struct
 * that is allocated with the alignment of page_t. ENOMEM when memory runs out.
 */
[[nodiscard]] page_t *page_array_create(size_t count);

/** Turns the page into an empty page of the given type. */
void page_init(page_t *page, page_type_t type);

[[nodiscard]] page_type_t page_type(const page_t *page);

[[nodiscard]] uint16_t page_flags(const page_t *page);
void page_set_flags(page_t *page, uint16_t flags);

/** Stores the CRC-32C of the page in its header. */
void page_set_checksum(page_t *page);

[[nodiscard]] int page_verify_checksum(const page_t *page);

[[nodiscard]] int page_validate(const page_t *page);
