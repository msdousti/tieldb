/**
 * In-memory tables.
 */

#include "storage/table.h"

#include <string.h>

struct table {
    const char *name;
    const table_column_t *columns;
    size_t column_count;
    const char *const *cells;
    size_t row_count;
};

static constexpr size_t USER_COLUMN_COUNT = 2;

static const table_column_t USER_COLUMNS[USER_COLUMN_COUNT] = {
    {"id", COLUMN_TYPE_INTEGER},
    {"name", COLUMN_TYPE_TEXT},
};

static const char *const USER_CELLS[] = {
    "1", "Alice", //
    "2", "Bob",   //
    "3", "Carol", //
};

static const table_t USER = {
    .name = "user",
    .columns = USER_COLUMNS,
    .column_count = USER_COLUMN_COUNT,
    .cells = USER_CELLS,
    .row_count = sizeof(USER_CELLS) / sizeof(USER_CELLS[0]) / USER_COLUMN_COUNT,
};

const table_t *table_find(const char *name)
{
    return strcmp(name, USER.name) == 0 ? &USER : nullptr;
}

size_t table_column_count(const table_t *table)
{
    return table->column_count;
}

const table_column_t *table_column(const table_t *table, size_t index)
{
    return &table->columns[index];
}

size_t table_row_count(const table_t *table)
{
    return table->row_count;
}

const char *table_cell(const table_t *table, size_t row, size_t column)
{
    return table->cells[(row * table->column_count) + column];
}
