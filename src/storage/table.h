/**
 * In-memory tables that can grow.
 *
 * A table has fixed columns and any number of rows, kept as text, which is what the wire protocol
 * sends. The name and column array passed to table_create are not copied and must outlive the
 * table; the cells are copied.
 *
 * Tables are shared between connection threads. table_insert locks the table itself. A reader
 * that needs a consistent view of several rows holds table_lock for the whole read, and must not
 * call table_insert in the meantime. Index arguments must be in range; callers get the ranges
 * from the table itself.
 *
 * There is also one built-in table, so queries can be answered before CREATE TABLE exists:
 *
 *   user (id INTEGER, name TEXT)   starts with three rows
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

/** Finds a table by its lower-case name; returns nullptr if there is none. */
[[nodiscard]] table_t *table_find(const char *name);

/** Appends a row; count must equal the number of columns. The text is copied. */
[[nodiscard]] int table_insert(table_t *table, const char *const *cells, size_t count);

void table_lock(table_t *table);
void table_unlock(table_t *table);

[[nodiscard]] size_t table_column_count(const table_t *table);
[[nodiscard]] const table_column_t *table_column(const table_t *table, size_t index);
[[nodiscard]] size_t table_row_count(const table_t *table);
[[nodiscard]] const char *table_cell(const table_t *table, size_t row, size_t column);
