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
    bool is_changed;
} frame_t;

struct buffer_pool {
    frame_t *frames;
    page_t *pages;
    size_t frame_count;
    hash_map_t *table;
    replacement_policy_t *policy;
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
    replacement_policy_destroy(pool->policy);
    free((void *) pool->files);
    free(pool->pages);
    free(pool->frames);
    free(pool);
}

static buffer_pool_t *reject_creation(replacement_policy_t *policy, int error)
{
    replacement_policy_destroy(policy);
    errno = error;
    return nullptr;
}

static bool allocate_tables(buffer_pool_t *pool)
{
    pool->frames = calloc(pool->frame_count, sizeof(*pool->frames));
    pool->pages = calloc(pool->frame_count, sizeof(*pool->pages));
    pool->table = hash_map_create(&PAGE_TABLE_OPS, pool->frame_count);
    return pool->frames != nullptr && pool->pages != nullptr && pool->table != nullptr;
}

buffer_pool_t *buffer_pool_create(size_t frame_count, replacement_policy_t *policy)
{
    if (policy == nullptr) {
        errno = EINVAL;
        return nullptr;
    }
    if (frame_count == 0) {
        return reject_creation(policy, EINVAL);
    }
    buffer_pool_t *pool = calloc(1, sizeof(*pool));
    if (pool == nullptr) {
        return reject_creation(policy, ENOMEM);
    }
    pool->policy = policy;
    pool->frame_count = frame_count;
    if (!allocate_tables(pool)) {
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
    return i == pool->frame_count ? nullptr : &pool->frames[i];
}

static size_t frame_index(const buffer_pool_t *pool, const frame_t *frame)
{
    return (size_t) (frame - pool->frames);
}

static page_t *frame_page(buffer_pool_t *pool, const frame_t *frame)
{
    return &pool->pages[frame_index(pool, frame)];
}

static int write_back(buffer_pool_t *pool, frame_t *frame)
{
    if (!frame->in_use || !frame->is_changed) {
        return 0;
    }
    int status = page_file_write(pool->files[frame->id.file_number], frame->id.page_number,
                                 frame_page(pool, frame));
    if (status == 0) {
        frame->is_changed = false;
    }
    return status;
}

// Reuses the frame of a page that nobody has pinned, after writing the page back if it changed
static frame_t *evict_page(buffer_pool_t *pool)
{
    size_t index = 0;
    if (replacement_policy_pick_victim(pool->policy, &index) != 0) {
        return nullptr;
    }
    frame_t *victim = &pool->frames[index];
    if (write_back(pool, victim) != 0) {
        replacement_policy_set_evictable(pool->policy, index, true);
        return nullptr;
    }
    (void) hash_map_remove(pool->table, &victim->id);
    victim->in_use = false;
    return victim;
}

static frame_t *acquire_frame(buffer_pool_t *pool)
{
    frame_t *frame = find_free_frame(pool);
    return frame != nullptr ? frame : evict_page(pool);
}

static frame_t *load_page(buffer_pool_t *pool, page_id_t id)
{
    if (id.file_number >= pool->file_count) {
        errno = EINVAL;
        return nullptr;
    }
    frame_t *frame = acquire_frame(pool);
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
    replacement_policy_record_access(pool->policy, frame_index(pool, frame));
    replacement_policy_set_evictable(pool->policy, frame_index(pool, frame), false);
    frame->pin_count++;
    *page = frame_page(pool, frame);
    return 0;
}

static frame_t *find_pinned_frame(buffer_pool_t *pool, const page_id_t *id)
{
    frame_t *frame = find_frame(pool, id);
    if (frame == nullptr || frame->pin_count == 0) {
        errno = EINVAL;
        return nullptr;
    }
    return frame;
}

int buffer_pool_unpin(buffer_pool_t *pool, page_id_t id)
{
    frame_t *frame = find_pinned_frame(pool, &id);
    if (frame == nullptr) {
        return -1;
    }
    frame->pin_count--;
    if (frame->pin_count == 0) {
        replacement_policy_set_evictable(pool->policy, frame_index(pool, frame), true);
    }
    return 0;
}

int buffer_pool_mark_changed(buffer_pool_t *pool, page_id_t id)
{
    frame_t *frame = find_pinned_frame(pool, &id);
    if (frame == nullptr) {
        return -1;
    }
    frame->is_changed = true;
    return 0;
}

int buffer_pool_flush_all(buffer_pool_t *pool)
{
    int status = 0;
    for (size_t i = 0; status == 0 && i < pool->frame_count; i++) {
        status = write_back(pool, &pool->frames[i]);
    }
    for (size_t i = 0; status == 0 && i < pool->file_count; i++) {
        status = page_file_sync(pool->files[i]);
    }
    return status;
}
