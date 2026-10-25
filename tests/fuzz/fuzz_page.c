/**
 * Fuzz target for pages read from disk: whatever the bytes, page_validate must answer without
 * crashing, and a page it accepts must be safe to read with every other function.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "storage/page.h"

static void read_tuple(const page_t *page, size_t slot)
{
    size_t length = 0;
    const unsigned char *tuple = page_get(page, slot, &length);
    if (tuple == nullptr || length == 0) {
        return;
    }
    (void) tuple[0];
    (void) tuple[length - 1];
}

static void read_every_slot(const page_t *page)
{
    for (size_t slot = 0; slot < page_slot_count(page); slot++) {
        read_tuple(page, slot);
    }
}

// libFuzzer looks this function up by name, so it cannot be static
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) // NOLINT(misc-use-internal-linkage)
{
    static page_t page;

    memset(&page, 0, sizeof(page));
    memcpy(&page, data, size < PAGE_SIZE ? size : PAGE_SIZE);
    (void) page_verify_checksum(&page);
    if (page_validate(&page) != 0) {
        return 0;
    }
    (void) page_type(&page);
    (void) page_flags(&page);
    (void) page_free_space(&page);
    read_every_slot(&page);
    return 0;
}
