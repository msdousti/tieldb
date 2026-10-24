/**
 * The catalog file: the tables of the database, saved to a file.
 *
 * The file is one page (one_page_file.h) of type PAGE_TYPE_CATALOG, so a catalog of a few dozen
 * tables fits. Its rows are written with the row codec, all with the same five columns: a kind, a
 * table name, a name, and two numbers, where unused ones are NULL.
 *
 *   version  (kind 0): the format version in the first number; always the first row
 *   table    (kind 1): the table name, and the name of its storage engine
 *   column   (kind 2): the table name, the column name, the type OID, and the display position
 *
 * A table row is followed by the rows of its columns, in the stored (physical) order. The format
 * version is CATALOG_FILE_FORMAT_VERSION; a reader refuses a file of another version.
 *
 * catalog_file_save writes the given tables, locking each one while it reads its columns. The file
 * does not say how rows are stored: each table's rows are in a file of the table's engine.
 *
 * Errors return -1 and set errno: ENOSPC when the tables do not fit in one page, EFBIG for a name
 * that is far too long, and those of one_page_file.h.
 */

#pragma once

#include <stddef.h>

#include "storage/table.h"

constexpr int CATALOG_FILE_FORMAT_VERSION = 1;

[[nodiscard]] int catalog_file_save(const char *path, table_t *const *tables, size_t count);
