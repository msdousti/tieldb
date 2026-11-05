#include "storage/page.h"

#include <errno.h>
#include <stdckdint.h>
#include <stdlib.h>

#include "storage/crc32c.h"
#include "storage/page_layout.h"
#include "storage/slotted_page.h"
#include "storage/slotted_page_check.h"

// Keep this the value after the last page_type_t, or page_validate rejects the new type
static constexpr unsigned PAGE_TYPE_LIMIT = PAGE_TYPE_CATALOG + 1;

void page_init(page_t *page, page_type_t type)
{
    memset(page->bytes, 0, PAGE_SIZE);
    page_header_t header = {.type = (uint16_t) type, .upper = PAGE_SIZE};
    page_header_store(page, &header);
}

page_type_t page_type(const page_t *page)
{
    return (page_type_t) page_header_load(page).type;
}

uint16_t page_flags(const page_t *page)
{
    return page_header_load(page).flags;
}

void page_set_flags(page_t *page, uint16_t flags)
{
    page_header_t header = page_header_load(page);
    header.flags = flags;
    page_header_store(page, &header);
}

static uint32_t compute_checksum(const page_t *page)
{
    page_t copy = *page;
    page_header_t header = page_header_load(&copy);
    header.checksum = 0;
    page_header_store(&copy, &header);
    return crc32c(copy.bytes, PAGE_SIZE);
}

void page_set_checksum(page_t *page)
{
    page_header_t header = page_header_load(page);
    header.checksum = compute_checksum(page);
    page_header_store(page, &header);
}

int page_verify_checksum(const page_t *page)
{
    if (page_header_load(page).checksum != compute_checksum(page)) {
        errno = EBADMSG;
        return -1;
    }
    return 0;
}

int page_validate(const page_t *page)
{
    page_header_t header = page_header_load(page);
    bool upper_is_valid = header.upper >= PAGE_HEADER_SIZE && header.upper <= PAGE_SIZE;
    if (header.type >= PAGE_TYPE_LIMIT || !upper_is_valid) {
        errno = EBADMSG;
        return -1;
    }
    return slotted_page_is_type(page_type(page)) ? slotted_page_validate(page) : 0;
}

// aligned_alloc does not check count * sizeof(page_t) the way calloc does
page_t *page_array_create(size_t count)
{
    size_t bytes = 0;
    if (ckd_mul(&bytes, count, sizeof(page_t))) {
        errno = ENOMEM;
        return nullptr;
    }
    return aligned_alloc(alignof(page_t), bytes);
}
