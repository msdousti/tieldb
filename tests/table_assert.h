/**
 * Assertion for the text of a table cell, shared by the tests that read tables.
 */

#pragma once

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "storage/table.h"

static inline void assert_cell_text(const table_t *table, size_t row, size_t column,
                                    const char *expected)
{
    char text[64];
    table_value_t cell = table_cell(table, row, column);
    const type_info_t *type = table_column(table, column)->type;
    assert_true(type->output(cell.data, cell.length, text, sizeof(text)) >= 0);
    assert_string_equal(text, expected);
}
