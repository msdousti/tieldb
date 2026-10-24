/**
 * The catalog file: the tables of the database, saved to a file and loaded again.
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
 * version is CATALOG_FILE_FORMAT_VERSION; a file of another version is refused, never guessed at.
 *
 * catalog_file_save writes the given tables, locking each one while it reads its columns.
 * catalog_file_load reads the file and calls the visitor once for each table, in the order of the
 * file; the strings and arrays it is given are valid during the call only. The visitor returns 0,
 * or -1 with errno set, which stops the load and is returned. The file does not say how rows are
 * stored: each table's rows are in a file of the table's engine.
 *
 * Errors return -1 and set errno: ENOSPC when the tables do not fit in one page, EFBIG for a name
 * that is far too long, those of one_page_file.h (ENOENT for a missing file, EBADMSG for a corrupt
 * one), and EBADMSG for a file that is intact but is not a catalog of this format: another
 * version, a column outside a table, a name of CATALOG_FILE_NAME_CAPACITY or more bytes, a type
 * that is not in the registry, more than TABLE_MAX_COLUMNS columns.
 */

#pragma once

#include <stddef.h>

#include "storage/table.h"

constexpr int CATALOG_FILE_FORMAT_VERSION = 1;

/** The size of a name with its terminating zero; the SQL layer accepts no longer identifiers. */
constexpr size_t CATALOG_FILE_NAME_CAPACITY = 64;

typedef struct {
    const char *name;
    const char *engine;
    size_t column_count;
    const table_column_t *columns;  // in the stored order
    const size_t *display_position; // of each column, in the same order
} catalog_file_table_t;

typedef int (*catalog_file_visitor_t)(const catalog_file_table_t *table, void *context);

[[nodiscard]] int catalog_file_save(const char *path, table_t *const *tables, size_t count);

[[nodiscard]] int catalog_file_load(const char *path, catalog_file_visitor_t visit, void *context);
