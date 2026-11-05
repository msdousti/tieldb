#include "storage/free_space_map.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "storage/fsm_page.h"

struct free_space_map {
    buffer_pool_t *pool;
    size_t file_number;
};

static constexpr size_t CATEGORY_SIZE = FREE_SPACE_MAP_CATEGORY_SIZE;
static constexpr size_t PAGE_ZERO = 0;
// The most free space that a page can report: all of the body, which holds nothing else
static constexpr size_t MOST_FREE_BYTES = PAGE_SIZE - PAGE_HEADER_SIZE;

static_assert(MOST_FREE_BYTES / CATEGORY_SIZE <= UINT8_MAX);

static int reset_page_zero(page_file_t *file)
{
    page_t page;
    fsm_page_t fsm;

    fsm_page_init(&fsm, &page);
    return page_file_write(file, PAGE_ZERO, &page);
}

static int check_type(const page_t *page)
{
    if (page_type(page) == PAGE_TYPE_FSM) {
        return 0;
    }
    errno = EBADMSG;
    return -1;
}

static int check_page_zero(page_file_t *file)
{
    page_t page;

    if (page_file_read(file, PAGE_ZERO, &page) == 0) {
        return check_type(&page);
    }
    return errno == EBADMSG ? reset_page_zero(file) : -1;
}

static int prepare_file(page_file_t *file)
{
    size_t count = 0;

    if (page_file_count_pages(file, &count) != 0) {
        return -1;
    }
    if (count == 0) {
        return page_file_allocate_page(file, PAGE_TYPE_FSM, &count);
    }
    return check_page_zero(file);
}

free_space_map_t *free_space_map_create(buffer_pool_t *pool, page_file_t *file)
{
    free_space_map_t *map = calloc(1, sizeof(*map));
    if (map == nullptr) {
        return nullptr;
    }
    map->pool = pool;
    if (prepare_file(file) != 0 || buffer_pool_add_file(pool, file, &map->file_number) != 0) {
        free(map);
        return nullptr;
    }
    return map;
}

void free_space_map_destroy(free_space_map_t *map)
{
    free(map);
}

static page_id_t page_zero_of(const free_space_map_t *map)
{
    return (page_id_t){.file_number = map->file_number, .page_number = PAGE_ZERO};
}

// Pins page 0 and makes tree a view of it. On failure nothing stays pinned
static int pin_tree(const free_space_map_t *map, max_tree_t *tree)
{
    page_t *page = nullptr;
    fsm_page_t fsm;

    if (buffer_pool_pin(map->pool, page_zero_of(map), &page) != 0) {
        return -1;
    }
    if (fsm_page_open(&fsm, page) != 0) {
        (void) buffer_pool_unpin(map->pool, page_zero_of(map));
        errno = EBADMSG;
        return -1;
    }
    fsm_page_tree(&fsm, tree);
    return 0;
}

// Unpins page 0 and returns the status, keeping the errno of a failure
static int release(const free_space_map_t *map, int status)
{
    int error = errno;
    int unpin_status = buffer_pool_unpin(map->pool, page_zero_of(map));

    if (status != 0) {
        errno = error;
        return -1;
    }
    return unpin_status;
}

int free_space_map_record(free_space_map_t *map, size_t heap_page, size_t free_bytes)
{
    max_tree_t tree;

    if (free_bytes > MOST_FREE_BYTES) {
        errno = EINVAL;
        return -1;
    }
    if (pin_tree(map, &tree) != 0) {
        return -1;
    }
    int status = max_tree_set(&tree, heap_page, (uint8_t) (free_bytes / CATEGORY_SIZE));
    if (status != 0) {
        errno = ENOSPC;
        return release(map, status);
    }
    return release(map, buffer_pool_mark_changed(map->pool, page_zero_of(map)));
}

int free_space_map_find(const free_space_map_t *map, size_t needed_bytes, size_t *heap_page)
{
    max_tree_t tree;
    size_t leaf = 0;

    if (needed_bytes > FREE_SPACE_MAP_LARGEST_REQUEST) {
        errno = ENOENT;
        return -1;
    }
    if (pin_tree(map, &tree) != 0) {
        return -1;
    }
    uint8_t category = (uint8_t) ((needed_bytes + CATEGORY_SIZE - 1) / CATEGORY_SIZE);
    int status = max_tree_find(&tree, category, &leaf);
    if (status == 0) {
        *heap_page = leaf;
    }
    return release(map, status);
}
