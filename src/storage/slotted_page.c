#include "storage/slotted_page.h"

#include <errno.h>
#include <string.h>

#include "storage/page_layout.h"

// A slot on the page: the state in the top bits of the first word, then the 14-bit offset, then
// the length
typedef struct {
    uint16_t state_and_offset;
    uint16_t length;
} stored_slot_t;

typedef struct {
    slot_state_t state;
    uint16_t offset;
    uint16_t length;
} slot_t;

static constexpr unsigned STATE_SHIFT = 14;
static constexpr uint16_t OFFSET_MASK = (1U << STATE_SHIFT) - 1;

static_assert(sizeof(stored_slot_t) == 4);
static_assert(PAGE_SIZE <= OFFSET_MASK);

static size_t slot_position(size_t slot)
{
    return PAGE_HEADER_SIZE + (slot * sizeof(stored_slot_t));
}

static slot_t load_slot(const page_t *page, size_t slot)
{
    stored_slot_t stored;
    memcpy(&stored, page->bytes + slot_position(slot), sizeof(stored));
    return (slot_t){.state = (slot_state_t) (stored.state_and_offset >> STATE_SHIFT),
                    .offset = stored.state_and_offset & OFFSET_MASK,
                    .length = stored.length};
}

static void store_slot(page_t *page, size_t slot, slot_t value)
{
    stored_slot_t stored = {.state_and_offset =
                                (uint16_t) (((unsigned) value.state << STATE_SHIFT) | value.offset),
                            .length = value.length};
    memcpy(page->bytes + slot_position(slot), &stored, sizeof(stored));
}

bool slotted_page_is_type(page_type_t type)
{
    return type == PAGE_TYPE_HEAP || type == PAGE_TYPE_CATALOG;
}

int slotted_page_open(slotted_page_t *slotted, page_t *page, slotted_page_kind_t kind)
{
    if (page_type(page) != (page_type_t) kind) {
        errno = EINVAL;
        return -1;
    }
    slotted->page = page;
    return 0;
}

void slotted_page_init(slotted_page_t *slotted, page_t *page, slotted_page_kind_t kind)
{
    page_init(page, (page_type_t) kind);
    slotted->page = page;
}

size_t slotted_page_slot_count(const slotted_page_t *slotted)
{
    return page_header_load(slotted->page).slot_count;
}

static size_t find_unused_slot(const page_t *page, const page_header_t *header)
{
    size_t slot = 0;
    while (slot < header->slot_count && load_slot(page, slot).state != SLOT_UNUSED) {
        slot++;
    }
    return slot;
}

static size_t reclaimable_space(const page_t *page, const page_header_t *header)
{
    size_t space = PAGE_SIZE - slot_position(header->slot_count);
    for (size_t slot = 0; slot < header->slot_count; slot++) {
        slot_t entry = load_slot(page, slot);
        space -= entry.state == SLOT_NORMAL ? entry.length : 0;
    }
    return space;
}

static size_t new_slot_cost(const page_header_t *header, size_t slot)
{
    return slot == header->slot_count ? sizeof(stored_slot_t) : 0;
}

size_t slotted_page_free_space(const slotted_page_t *slotted)
{
    const page_t *page = slotted->page;
    page_header_t header = page_header_load(page);
    size_t cost = new_slot_cost(&header, find_unused_slot(page, &header));
    size_t space = reclaimable_space(page, &header);
    return space > cost ? space - cost : 0;
}

static void move_tuple(page_t *page, const page_t *before, page_header_t *header, size_t slot)
{
    slot_t entry = load_slot(before, slot);
    if (entry.state != SLOT_NORMAL) {
        return;
    }
    header->upper = (uint16_t) (header->upper - entry.length);
    memcpy(page->bytes + header->upper, before->bytes + entry.offset, entry.length);
    store_slot(page, slot, (slot_t){SLOT_NORMAL, header->upper, entry.length});
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

int slotted_page_insert(slotted_page_t *slotted, const void *tuple, size_t length)
{
    page_t *page = slotted->page;
    page_header_t header = page_header_load(page);
    size_t slot = find_unused_slot(page, &header);
    if (!make_room(page, &header, length + new_slot_cost(&header, slot))) {
        return -1;
    }
    write_tuple(page, &header, tuple, length);
    store_slot(page, slot, (slot_t){SLOT_NORMAL, header.upper, (uint16_t) length});
    if (slot == header.slot_count) {
        header.slot_count++;
    }
    page_header_store(page, &header);
    return (int) slot;
}

static bool is_live_slot(const page_t *page, size_t slot)
{
    return slot < page_header_load(page).slot_count && load_slot(page, slot).state == SLOT_NORMAL;
}

const void *slotted_page_get(const slotted_page_t *slotted, size_t slot, size_t *length)
{
    const page_t *page = slotted->page;
    if (!is_live_slot(page, slot)) {
        errno = ENOENT;
        return nullptr;
    }
    slot_t entry = load_slot(page, slot);
    *length = entry.length;
    return page->bytes + entry.offset;
}

int slotted_page_delete(slotted_page_t *slotted, size_t slot)
{
    page_t *page = slotted->page;
    if (!is_live_slot(page, slot)) {
        errno = ENOENT;
        return -1;
    }
    store_slot(page, slot, (slot_t){SLOT_DEAD, 0, 0});
    return 0;
}

static size_t reclaim_slot(page_t *page, size_t slot)
{
    if (load_slot(page, slot).state != SLOT_DEAD) {
        return 0;
    }
    store_slot(page, slot, (slot_t){SLOT_UNUSED, 0, 0});
    return 1;
}

size_t slotted_page_reclaim_dead_slots(slotted_page_t *slotted)
{
    page_t *page = slotted->page;
    size_t reclaimed = 0;
    for (size_t slot = 0; slot < page_header_load(page).slot_count; slot++) {
        reclaimed += reclaim_slot(page, slot);
    }
    return reclaimed;
}

static bool is_valid_slot(const page_t *page, const page_header_t *header, size_t slot)
{
    slot_t entry = load_slot(page, slot);
    if (entry.state > SLOT_DEAD) {
        return false;
    }
    return entry.state != SLOT_NORMAL ||
           (entry.offset >= header->upper && entry.offset + entry.length <= PAGE_SIZE);
}

int slotted_page_validate(const page_t *page)
{
    page_header_t header = page_header_load(page);
    bool is_valid = slot_position(header.slot_count) <= header.upper;
    for (size_t slot = 0; is_valid && slot < header.slot_count; slot++) {
        is_valid = is_valid_slot(page, &header, slot);
    }
    if (!is_valid) {
        errno = EBADMSG;
        return -1;
    }
    return 0;
}
