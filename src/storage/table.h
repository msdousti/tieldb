/**
 * In-memory tables that can grow.
 *
 * A table has a name, fixed columns (0 to TABLE_MAX_COLUMNS of them) and any number of rows, kept
 * as text, which is what the wire protocol sends. table_create copies the name and the column
 * names, so the arguments need not outlive the call; table_insert copies the cells.
 *
 * Columns are stored in the order they were defined (the physical order). They are shown in the
 * display order, which starts out the same and can be changed with table_set_display_order
 * without touching the stored rows. Statements that name columns by position, such as
 * SELECT * and INSERT ... VALUES, follow the display order: display index d refers to the column
 * table_physical_index(d). table_column and table_cell take physical indexes.
 *
 * Tables are shared between connection threads. table_insert locks the table itself. A reader
 * that needs a consistent view of several rows holds table_lock for the whole read, and must not
 * call table_insert in the meantime. table_physical_index, too, must be called with the lock held,
 * because the display order can change. Index arguments must be in range; callers get the ranges
 * from the table itself. Tables are found by name through the catalog (catalog.h).
 *
 * Errors return -1 or nullptr and set errno: EINVAL for a null argument or a wrong cell count,
 * ENOMEM when memory runs out. A failed table_insert leaves the table unchanged.
 */

#pragma once

#include <stddef.h>

/** Most columns a table may have. */
constexpr size_t TABLE_MAX_COLUMNS = 32;

typedef enum {
    COLUMN_TYPE_INTEGER,
    COLUMN_TYPE_TEXT,
} column_type_t;

typedef struct {
    const char *name;
    column_type_t type;
} table_column_t;

typedef struct table table_t;

[[nodiscard]] table_t *table_create(const char *name, const table_column_t *columns,
                                    size_t column_count);
void table_destroy(table_t *table);

/** Appends a row; count must equal the number of columns. The text is copied. */
[[nodiscard]] int table_insert(table_t *table, const char *const *cells, size_t count);

/**
 * Like table_insert, for a caller that already holds table_lock, for example because it looked up
 * display positions first.
 */
[[nodiscard]] int table_insert_locked(table_t *table, const char *const *cells, size_t count);

/**
 * Sets the display order: order[d] is the physical index of the column shown at display index d.
 * It must list every column exactly once, otherwise errno is EINVAL. Takes the lock itself.
 */
[[nodiscard]] int table_set_display_order(table_t *table, const size_t *order, size_t count);

void table_lock(table_t *table);
void table_unlock(table_t *table);

[[nodiscard]] const char *table_name(const table_t *table);
[[nodiscard]] size_t table_column_count(const table_t *table);
[[nodiscard]] size_t table_physical_index(const table_t *table, size_t display_index);
[[nodiscard]] const table_column_t *table_column(const table_t *table, size_t index);
[[nodiscard]] size_t table_row_count(const table_t *table);
[[nodiscard]] const char *table_cell(const table_t *table, size_t row, size_t column);
