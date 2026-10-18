#include "storage/page.h"

#include <errno.h>
#include <string.h>

typedef struct {
    uint64_t lsn;
    uint32_t checksum;
    uint16_t type;
    uint16_t slot_count;
    uint16_t upper; // offset of the lowest tuple
    uint16_t reserved;
} page_header_t;

typedef struct {
    uint16_t offset;
    uint16_t length;
} slot_t;

static constexpr size_t PAGE_HEADER_SIZE = 24;

static_assert(sizeof(page_header_t) == PAGE_HEADER_SIZE);
static_assert(sizeof(slot_t) == 4);
static_assert(PAGE_SIZE <= UINT16_MAX);

static constexpr uint16_t DELETED_OFFSET = 0;

static page_header_t load_header(const page_t *page)
{
    page_header_t header;
    memcpy(&header, page->bytes, sizeof(header));
    return header;
}

static void store_header(page_t *page, const page_header_t *header)
{
    memcpy(page->bytes, header, sizeof(*header));
}

static size_t slot_position(size_t slot)
{
    return PAGE_HEADER_SIZE + (slot * sizeof(slot_t));
}

static slot_t load_slot(const page_t *page, size_t slot)
{
    slot_t result;
    memcpy(&result, page->bytes + slot_position(slot), sizeof(result));
    return result;
}

static void store_slot(page_t *page, size_t slot, slot_t value)
{
    memcpy(page->bytes + slot_position(slot), &value, sizeof(value));
}

void page_init(page_t *page, page_type_t type)
{
    memset(page->bytes, 0, PAGE_SIZE);
    page_header_t header = {.type = (uint16_t) type, .upper = PAGE_SIZE};
    store_header(page, &header);
}

page_type_t page_type(const page_t *page)
{
    return (page_type_t) load_header(page).type;
}

size_t page_slot_count(const page_t *page)
{
    return load_header(page).slot_count;
}

static size_t find_free_slot(const page_t *page, const page_header_t *header)
{
    size_t slot = 0;
    while (slot < header->slot_count && load_slot(page, slot).offset != DELETED_OFFSET) {
        slot++;
    }
    return slot;
}

static size_t reclaimable_space(const page_t *page, const page_header_t *header)
{
    size_t space = PAGE_SIZE - slot_position(header->slot_count);
    for (size_t slot = 0; slot < header->slot_count; slot++) {
        slot_t entry = load_slot(page, slot);
        space -= entry.offset == DELETED_OFFSET ? 0 : entry.length;
    }
    return space;
}

static size_t new_slot_cost(const page_header_t *header, size_t slot)
{
    return slot == header->slot_count ? sizeof(slot_t) : 0;
}

size_t page_free_space(const page_t *page)
{
    page_header_t header = load_header(page);
    size_t cost = new_slot_cost(&header, find_free_slot(page, &header));
    size_t space = reclaimable_space(page, &header);
    return space > cost ? space - cost : 0;
}

static void move_tuple(page_t *page, const page_t *before, page_header_t *header, size_t slot)
{
    slot_t entry = load_slot(before, slot);
    if (entry.offset == DELETED_OFFSET) {
        return;
    }
    header->upper = (uint16_t) (header->upper - entry.length);
    memcpy(page->bytes + header->upper, before->bytes + entry.offset, entry.length);
    store_slot(page, slot, (slot_t){header->upper, entry.length});
}

static void compact(page_t *page, page_header_t *header)
{
    page_t before = *page;
    header->upper = PAGE_SIZE;
    for (size_t slot = 0; slot < header->slot_count; slot++) {
        move_tuple(page, &before, header, slot);
    }
}

static bool make_room(page_t *page, page_header_t *header, size_t needed)
{
    if (needed > reclaimable_space(page, header)) {
        errno = ENOSPC;
        return false;
    }
    if (needed > header->upper - slot_position(header->slot_count)) {
        compact(page, header);
    }
    return true;
}

static void write_tuple(page_t *page, page_header_t *header, const void *tuple, size_t length)
{
    header->upper = (uint16_t) (header->upper - length);
    memcpy(page->bytes + header->upper, tuple, length);
}

int page_insert(page_t *page, const void *tuple, size_t length)
{
    page_header_t header = load_header(page);
    size_t slot = find_free_slot(page, &header);
    if (!make_room(page, &header, length + new_slot_cost(&header, slot))) {
        return -1;
    }
    write_tuple(page, &header, tuple, length);
    store_slot(page, slot, (slot_t){header.upper, (uint16_t) length});
    header.slot_count = (uint16_t) (slot == header.slot_count ? slot + 1 : header.slot_count);
    store_header(page, &header);
    return (int) slot;
}

static bool is_live_slot(const page_t *page, size_t slot)
{
    return slot < page_slot_count(page) && load_slot(page, slot).offset != DELETED_OFFSET;
}

const void *page_get(const page_t *page, size_t slot, size_t *length)
{
    if (!is_live_slot(page, slot)) {
        errno = ENOENT;
        return nullptr;
    }
    slot_t entry = load_slot(page, slot);
    *length = entry.length;
    return page->bytes + entry.offset;
}

int page_delete(page_t *page, size_t slot)
{
    if (!is_live_slot(page, slot)) {
        errno = ENOENT;
        return -1;
    }
    store_slot(page, slot, (slot_t){DELETED_OFFSET, 0});
    return 0;
}
