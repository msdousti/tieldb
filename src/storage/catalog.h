/**
 * The catalog: every table, found by name.
 *
 * Names are lower-case, as the SQL layer produces them. The catalog is shared between connection
 * threads and locks itself. Tables live as long as the process; there is no DROP TABLE yet.
 *
 * The catalog starts empty.
 *
 * catalog_create_table takes 0 to TABLE_MAX_COLUMNS columns and copies its arguments. Errors
 * return nullptr and set errno: EEXIST when a table of that name exists, ENOMEM when memory runs
 * out. On failure the catalog is unchanged.
 */

#pragma once

#include "storage/table.h"

/** Returns the table of that name, or nullptr if there is none. */
[[nodiscard]] table_t *catalog_find(const char *name);

[[nodiscard]] table_t *catalog_create_table(const char *name, const table_column_t *columns,
                                            size_t column_count);
