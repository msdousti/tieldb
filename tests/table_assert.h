/**
 * Helpers for the tests that read tables: the number of rows, and the text of a cell.
 */

#pragma once

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "storage/table.h"

static inline size_t count_rows(const table_t *table)
{
    storage_cursor_t *cursor = storage_open_cursor(table_engine(table));
    row_value_t cells[TABLE_MAX_COLUMNS];
    size_t count = 0;

    while (storage_next(cursor, cells) == 1) {
        count++;
    }
    storage_close_cursor(cursor);
    return count;
}

static inline void assert_cell_text(const table_t *table, size_t row, size_t column,
                                    const char *expected)
{
    storage_cursor_t *cursor = storage_open_cursor(table_engine(table));
    row_value_t cells[TABLE_MAX_COLUMNS];
    char text[64];

    for (size_t i = 0; i <= row; i++) {
        assert_int_equal(storage_next(cursor, cells), 1);
    }
    const type_info_t *type = table_column(table, column)->type;
    assert_true(type->output(cells[column].data, cells[column].length, text, sizeof(text)) >= 0);
    assert_string_equal(text, expected);
    storage_close_cursor(cursor);
}
