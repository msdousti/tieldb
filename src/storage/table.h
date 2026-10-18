/**
 * In-memory tables.
 *
 * For now there is a single hardcoded table so queries can be answered end to end before the
 * catalog, INSERT and CREATE TABLE exist; those will replace the hardcoded data.
 *
 *   user (id INTEGER, name TEXT)   three rows
 *
 * Cells are kept in their text form, which is what the wire protocol sends. Index arguments must
 * be in range; callers get the ranges from the table itself.
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

/** Finds a table by its lower-case name; returns nullptr if there is none. */
[[nodiscard]] const table_t *table_find(const char *name);
[[nodiscard]] size_t table_column_count(const table_t *table);
[[nodiscard]] const table_column_t *table_column(const table_t *table, size_t index);
[[nodiscard]] size_t table_row_count(const table_t *table);
[[nodiscard]] const char *table_cell(const table_t *table, size_t row, size_t column);
