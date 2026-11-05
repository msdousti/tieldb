#include "storage/free_space_map.h"

#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>

#include "storage/fsm_page.h"

struct free_space_map {
    pthread_mutex_t lock;
    buffer_pool_t *pool;
    size_t file_number;
};

static constexpr size_t CATEGORY_SIZE = FREE_SPACE_MAP_CATEGORY_SIZE;
static constexpr size_t PAGE_ZERO = 0;
// The most free space that a page can report: all of the body, which holds nothing else
static constexpr size_t MOST_FREE_BYTES = FSM_PAGE_BODY_SIZE;

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
    map->lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    map->pool = pool;
    if (prepare_file(file) != 0 || buffer_pool_add_file(pool, file, &map->file_number) != 0) {
        free(map);
        return nullptr;
    }
    return map;
}

void free_space_map_destroy(free_space_map_t *map)
{
    pthread_mutex_destroy(&map->lock);
    free(map);
}

static page_id_t page_zero_of(const free_space_map_t *map)
{
    return (page_id_t){.file_number = map->file_number, .page_number = PAGE_ZERO};
}

// Page 0 of the map, pinned, with the view of its tree
typedef struct {
    fsm_page_t fsm;
    max_tree_t tree;
} pinned_tree_t;

// On failure nothing stays pinned
static int pin_tree(const free_space_map_t *map, pinned_tree_t *pinned)
{
    page_t *page = nullptr;

    if (buffer_pool_pin(map->pool, page_zero_of(map), &page) != 0) {
        return -1;
    }
    if (fsm_page_open(&pinned->fsm, page) != 0) {
        (void) buffer_pool_unpin(map->pool, page_zero_of(map));
        errno = EBADMSG;
        return -1;
    }
    fsm_page_tree(&pinned->fsm, &pinned->tree);
    return 0;
}

// The unpin must not overwrite the errno of a failure
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

static int record_locked(const free_space_map_t *map, size_t heap_page, size_t free_bytes)
{
    pinned_tree_t pinned;

    if (free_bytes > MOST_FREE_BYTES) {
        errno = EINVAL;
        return -1;
    }
    if (pin_tree(map, &pinned) != 0) {
        return -1;
    }
    int status = max_tree_set(&pinned.tree, heap_page, (uint8_t) (free_bytes / CATEGORY_SIZE));
    if (status != 0) {
        errno = ENOSPC;
        return release(map, status);
    }
    return release(map, buffer_pool_mark_changed(map->pool, page_zero_of(map)));
}

static int repair_and_search(const free_space_map_t *map, pinned_tree_t *pinned, uint8_t category,
                             size_t *leaf)
{
    fsm_page_rebuild_tree(&pinned->fsm, &pinned->tree);
    int found = max_tree_find(&pinned->tree, category, leaf);
    int marked = buffer_pool_mark_changed(map->pool, page_zero_of(map));

    return found == 0 ? marked : found;
}

// A tree whose inner nodes promise too much is rebuilt from its leaves, and the search repeated
static int search_tree(const free_space_map_t *map, pinned_tree_t *pinned, uint8_t category,
                       size_t *leaf)
{
    int status = max_tree_find(&pinned->tree, category, leaf);

    if (status == 0 || errno != EBADMSG) {
        return status;
    }
    return repair_and_search(map, pinned, category, leaf);
}

static int find_locked(const free_space_map_t *map, size_t needed_bytes, size_t *heap_page)
{
    pinned_tree_t pinned;
    size_t leaf = 0;

    if (needed_bytes > FREE_SPACE_MAP_LARGEST_REQUEST) {
        errno = ENOENT;
        return -1;
    }
    if (pin_tree(map, &pinned) != 0) {
        return -1;
    }
    uint8_t category = (uint8_t) ((needed_bytes + CATEGORY_SIZE - 1) / CATEGORY_SIZE);
    int status = search_tree(map, &pinned, category, &leaf);
    if (status == 0) {
        *heap_page = leaf;
    }
    return release(map, status);
}

int free_space_map_record(free_space_map_t *map, size_t heap_page, size_t free_bytes)
{
    pthread_mutex_lock(&map->lock);
    int status = record_locked(map, heap_page, free_bytes);
    int error = errno;
    pthread_mutex_unlock(&map->lock);
    errno = error;
    return status;
}

int free_space_map_find(free_space_map_t *map, size_t needed_bytes, size_t *heap_page)
{
    pthread_mutex_lock(&map->lock);
    int status = find_locked(map, needed_bytes, heap_page);
    int error = errno;
    pthread_mutex_unlock(&map->lock);
    errno = error;
    return status;
}
