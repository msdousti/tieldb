/**
 * The storage engine interface: how the rows of one table are stored, behind rows and cursors.
 *
 * An engine instance stores the rows of one table. The executor sees only rows (values in the
 * stored format of their column types, see row_codec.h) and cursors, never pages, tuple IDs or
 * files, so that engines can be swapped: the one-page engine (one_page_engine.h) first, the heap
 * engine and perhaps an LSM engine later.
 *
 * An implementation embeds storage_engine_t (and storage_cursor_t) as the first member of its own
 * struct and sets ops. The wrappers below call the operation of the engine behind the pointer.
 *
 * An engine does no locking. The caller serializes the calls on one engine, and holds that
 * serialization (for the executor, the table lock) from opening a cursor until it closes it. No
 * insert may happen while a cursor is open. Values in an insert are copied. The values that
 * storage_next returns point into the engine and are valid until the next call on that cursor.
 *
 * Errors: insert returns -1 and sets errno: EINVAL for a value that does not fit its type (see
 * row_codec.h), EFBIG for a row that no row of this engine could hold, ENOSPC when the engine is
 * full, ENOMEM. storage_open_cursor returns nullptr with ENOMEM. storage_next returns 1 for a row,
 * 0 at the end, and -1 with errno for an error.
 */

#pragma once

#include "storage/row_codec.h"

typedef struct storage_engine storage_engine_t;
typedef struct storage_cursor storage_cursor_t;

typedef struct {
    void (*destroy)(storage_engine_t *engine);
    int (*insert)(storage_engine_t *engine, const row_value_t *values);
    storage_cursor_t *(*open_cursor)(storage_engine_t *engine);
    int (*next)(storage_cursor_t *cursor, row_value_t *values);
    void (*close_cursor)(storage_cursor_t *cursor);
} storage_engine_ops_t;

struct storage_engine {
    const storage_engine_ops_t *ops;
};

struct storage_cursor {
    const storage_engine_ops_t *ops;
};

/** Frees the engine and its rows. All cursors must be closed. */
static inline void storage_destroy(storage_engine_t *engine)
{
    engine->ops->destroy(engine);
}

/** Adds a row: one value per column of the layout the engine was created with. */
[[nodiscard]] static inline int storage_insert(storage_engine_t *engine, const row_value_t *values)
{
    return engine->ops->insert(engine, values);
}

/** Opens a cursor before the first row; rows come in the order they were inserted. */
[[nodiscard]] static inline storage_cursor_t *storage_open_cursor(storage_engine_t *engine)
{
    return engine->ops->open_cursor(engine);
}

/** Moves to the next row and fills values, one per column. */
[[nodiscard]] static inline int storage_next(storage_cursor_t *cursor, row_value_t *values)
{
    return cursor->ops->next(cursor, values);
}

static inline void storage_close_cursor(storage_cursor_t *cursor)
{
    cursor->ops->close_cursor(cursor);
}
