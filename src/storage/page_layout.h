/**
 * The header that every page starts with. Private to the page modules (page.c, slotted_page.c),
 * which interpret the bytes after it: the slot array and the tuples, or whatever the page type
 * stores there. Nothing else includes this file.
 */

#pragma once

#include <stdint.h>
#include <string.h>

#include "storage/page.h"

typedef struct {
    uint64_t lsn;
    uint32_t checksum;
    uint16_t type;
    uint16_t flags;
    uint16_t slot_count;
    uint16_t upper; // offset of the lowest tuple, for a slotted page
} page_header_t;

static constexpr size_t PAGE_HEADER_SIZE = 24;

static_assert(sizeof(page_header_t) == PAGE_HEADER_SIZE);

static inline page_header_t page_header_load(const page_t *page)
{
    page_header_t header;
    memcpy(&header, page->bytes, sizeof(header));
    return header;
}

static inline void page_header_store(page_t *page, const page_header_t *header)
{
    memcpy(page->bytes, header, sizeof(*header));
}
