#include "storage/one_page_engine.h"

#include <errno.h>
#include <stdlib.h>

#include "storage/page.h"

typedef struct {
    storage_engine_t base;
    page_t page;
    size_t largest_row;
    size_t column_count;
    const type_info_t *types[];
} one_page_engine_t;

typedef struct {
    storage_cursor_t base;
    const one_page_engine_t *engine;
    size_t slot;
} one_page_cursor_t;

static const storage_engine_ops_t ONE_PAGE_OPS;

static row_layout_t layout_of(const one_page_engine_t *engine)
{
    return (row_layout_t){engine->types, engine->column_count};
}

storage_engine_t *one_page_engine_create(const row_layout_t *layout)
{
    size_t size = sizeof(one_page_engine_t) + (layout->count * sizeof(layout->types[0]));
    one_page_engine_t *engine = malloc(size);
    if (engine == nullptr) {
        return nullptr;
    }
    engine->base.ops = &ONE_PAGE_OPS;
    engine->column_count = layout->count;
    for (size_t column = 0; column < layout->count; column++) {
        engine->types[column] = layout->types[column];
    }
    page_init(&engine->page, PAGE_TYPE_HEAP);
    engine->largest_row = page_free_space(&engine->page);
    return &engine->base;
}

static void one_page_destroy(storage_engine_t *engine)
{
    free(engine);
}

static int one_page_insert(storage_engine_t *base, const row_value_t *values)
{
    one_page_engine_t *engine = (one_page_engine_t *) base;
    const row_layout_t layout = layout_of(engine);
    unsigned char row[PAGE_SIZE];
    ssize_t size = row_encode(&layout, values, row, sizeof(row));
    if (size < 0 && errno == E2BIG) {
        errno = EFBIG;
    }
    if (size < 0) {
        return -1;
    }
    if ((size_t) size > engine->largest_row) {
        errno = EFBIG;
        return -1;
    }
    return page_insert(&engine->page, row, (size_t) size) < 0 ? -1 : 0;
}

static storage_cursor_t *one_page_open_cursor(storage_engine_t *base)
{
    one_page_cursor_t *cursor = malloc(sizeof(*cursor));
    if (cursor == nullptr) {
        return nullptr;
    }
    *cursor = (one_page_cursor_t){.base = {base->ops}, .engine = (one_page_engine_t *) base};
    return &cursor->base;
}

static int one_page_next(storage_cursor_t *base, row_value_t *values)
{
    one_page_cursor_t *cursor = (one_page_cursor_t *) base;
    const row_layout_t layout = layout_of(cursor->engine);
    const void *row = nullptr;
    size_t length = 0;
    while (cursor->slot < page_slot_count(&cursor->engine->page) && row == nullptr) {
        row = page_get(&cursor->engine->page, cursor->slot++, &length);
    }
    if (row == nullptr) {
        return 0;
    }
    return row_decode(&layout, row, length, values) == 0 ? 1 : -1;
}

static void one_page_close_cursor(storage_cursor_t *cursor)
{
    free(cursor);
}

static const storage_engine_ops_t ONE_PAGE_OPS = {
    .destroy = one_page_destroy,
    .insert = one_page_insert,
    .open_cursor = one_page_open_cursor,
    .next = one_page_next,
    .close_cursor = one_page_close_cursor,
};
