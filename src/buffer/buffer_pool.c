#include "buffer/buffer_pool.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ds/hash_map.h"

typedef struct {
    hash_node_t node;
    page_id_t id;
    size_t pin_count;
    bool in_use;
} frame_t;

struct buffer_pool {
    frame_t *frames;
    page_t *pages;
    size_t frame_count;
    hash_map_t *table;
    page_file_t **files;
    size_t file_count;
};

static size_t hash_page_id(const void *key)
{
    return hash_bytes(key, sizeof(page_id_t));
}

static bool frame_has_id(const hash_node_t *entry, const void *key)
{
    const frame_t *frame = HASH_MAP_ENTRY(entry, const frame_t, node);
    return memcmp(&frame->id, key, sizeof(page_id_t)) == 0;
}

static const hash_map_ops_t PAGE_TABLE_OPS = {.hash = hash_page_id, .has_key = frame_has_id};

static void free_pool(buffer_pool_t *pool)
{
    if (pool->table != nullptr) {
        hash_map_destroy(pool->table);
    }
    free((void *) pool->files);
    free(pool->pages);
    free(pool->frames);
    free(pool);
}

buffer_pool_t *buffer_pool_create(size_t frame_count)
{
    if (frame_count == 0) {
        errno = EINVAL;
        return nullptr;
    }
    buffer_pool_t *pool = calloc(1, sizeof(*pool));
    if (pool == nullptr) {
        return nullptr;
    }
    pool->frame_count = frame_count;
    pool->frames = calloc(frame_count, sizeof(*pool->frames));
    pool->pages = calloc(frame_count, sizeof(*pool->pages));
    pool->table = hash_map_create(&PAGE_TABLE_OPS, frame_count);
    if (pool->frames == nullptr || pool->pages == nullptr || pool->table == nullptr) {
        free_pool(pool);
        errno = ENOMEM;
        return nullptr;
    }
    return pool;
}

int buffer_pool_destroy(buffer_pool_t *pool)
{
    size_t pinned = 0;
    for (size_t i = 0; i < pool->frame_count; i++) {
        pinned += pool->frames[i].pin_count > 0 ? 1 : 0;
    }
    free_pool(pool);
    if (pinned > 0) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int buffer_pool_add_file(buffer_pool_t *pool, page_file_t *file, size_t *file_number)
{
    page_file_t **files =
        (page_file_t **) realloc((void *) pool->files, (pool->file_count + 1) * sizeof(*files));
    if (files == nullptr) {
        return -1;
    }
    pool->files = files;
    pool->files[pool->file_count] = file;
    *file_number = pool->file_count++;
    return 0;
}

static frame_t *find_frame(const buffer_pool_t *pool, const page_id_t *id)
{
    return HASH_MAP_ENTRY(hash_map_find(pool->table, id), frame_t, node);
}

static frame_t *find_free_frame(buffer_pool_t *pool)
{
    size_t i = 0;
    while (i < pool->frame_count && pool->frames[i].in_use) {
        i++;
    }
    if (i == pool->frame_count) {
        errno = ENOBUFS;
        return nullptr;
    }
    return &pool->frames[i];
}

static page_t *frame_page(buffer_pool_t *pool, const frame_t *frame)
{
    return &pool->pages[frame - pool->frames];
}

static frame_t *load_page(buffer_pool_t *pool, page_id_t id)
{
    if (id.file_number >= pool->file_count) {
        errno = EINVAL;
        return nullptr;
    }
    frame_t *frame = find_free_frame(pool);
    if (frame == nullptr ||
        page_file_read(pool->files[id.file_number], id.page_number, frame_page(pool, frame)) != 0) {
        return nullptr;
    }
    *frame = (frame_t){.id = id, .in_use = true};
    hash_map_insert(pool->table, &frame->node, &id);
    return frame;
}

int buffer_pool_pin(buffer_pool_t *pool, page_id_t id, page_t **page)
{
    frame_t *frame = find_frame(pool, &id);
    frame = frame != nullptr ? frame : load_page(pool, id);
    if (frame == nullptr) {
        return -1;
    }
    frame->pin_count++;
    *page = frame_page(pool, frame);
    return 0;
}

int buffer_pool_unpin(buffer_pool_t *pool, page_id_t id)
{
    frame_t *frame = find_frame(pool, &id);
    if (frame == nullptr || frame->pin_count == 0) {
        errno = EINVAL;
        return -1;
    }
    frame->pin_count--;
    return 0;
}
