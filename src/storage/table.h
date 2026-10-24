/**
 * Tables: the catalog entry of a table, and its rows.
 *
 * A table has a name, fixed columns (0 to TABLE_MAX_COLUMNS of them) and rows. The rows live in a
 * storage engine instance that the table creates and owns (the one-page engine for now), in the
 * format of their column's type (an integer as 4 bytes, see type_registry.h). table_create copies
 * the name and the column names, so the arguments need not outlive the call; table_insert
 * converts the text of each cell to its column's type and gives the row to the engine.
 *
 * Columns are stored in the order they were defined (the physical order). They are shown in the
 * display order, which starts out the same and can be changed with table_set_display_order
 * without touching the stored rows. Statements that name columns by position, such as
 * SELECT * and INSERT ... VALUES, follow the display order: display index d refers to the column
 * table_physical_index(d). table_column takes physical indexes, as do the values of a stored row.
 *
 * Tables are shared between connection threads. table_insert locks the table itself. A reader
 * that needs a consistent view of several rows holds table_lock for the whole read, and must not
 * call table_insert in the meantime. table_physical_index, too, must be called with the lock held,
 * because the display order can change. Index arguments must be in range; callers get the ranges
 * from the table itself. Tables are found by name through the catalog (catalog.h).
 *
 * Errors return -1 or nullptr and set errno: EINVAL for a null argument, a wrong cell count or a
 * cell whose text is not a valid value of its column's type, ENOMEM when memory runs out, and
 * those of the engine for an insert (ENOSPC when the table is full, EFBIG for a row that could
 * never fit; see storage_engine.h). A failed table_insert leaves the table unchanged.
 */

#pragma once

#include <stddef.h>

#include "ds/hash_map.h"
#include "storage/storage_engine.h"
#include "types/type_registry.h"

/** Most columns a table may have. */
constexpr size_t TABLE_MAX_COLUMNS = 32;

typedef struct {
    const char *name;
    const type_info_t *type;
} table_column_t;

typedef struct table table_t;

[[nodiscard]] table_t *table_create(const char *name, const table_column_t *columns,
                                    size_t column_count);
void table_destroy(table_t *table);

/** Appends a row; count must equal the number of columns. Each text is converted to its column's
 * type. */
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

/**
 * A table embeds the link that lets the catalog keep it in a hash map. The two functions convert
 * between the table and its link; table_from_node(nullptr) is nullptr.
 */
[[nodiscard]] hash_node_t *table_node(table_t *table);
[[nodiscard]] table_t *table_from_node(const hash_node_t *node);
[[nodiscard]] size_t table_column_count(const table_t *table);
[[nodiscard]] size_t table_physical_index(const table_t *table, size_t display_index);
[[nodiscard]] const table_column_t *table_column(const table_t *table, size_t index);

/**
 * Saves the rows of the table to a file with the engine of the table (storage_save), holding the
 * table lock. Errors are those of the engine.
 */
[[nodiscard]] int table_save_rows(table_t *table, const char *path);

/**
 * The storage engine instance that holds the rows. Reading goes through a cursor on it
 * (storage_engine.h), with the table locked from opening the cursor until it is closed.
 */
[[nodiscard]] storage_engine_t *table_engine(const table_t *table);
