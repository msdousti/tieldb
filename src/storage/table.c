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

static constexpr size_t USERS_COLUMN_COUNT = 2;

static const table_column_t USERS_COLUMNS[USERS_COLUMN_COUNT] = {
    {"id", COLUMN_TYPE_INTEGER},
    {"name", COLUMN_TYPE_TEXT},
};

static const char *const USERS_CELLS[] = {
    "1", "Alice", //
    "2", "Bob",   //
    "3", "Carol", //
};

static const table_t USERS = {
    .name = "users",
    .columns = USERS_COLUMNS,
    .column_count = USERS_COLUMN_COUNT,
    .cells = USERS_CELLS,
    .row_count = sizeof(USERS_CELLS) / sizeof(USERS_CELLS[0]) / USERS_COLUMN_COUNT,
};

const table_t *table_find(const char *name)
{
    return strcmp(name, USERS.name) == 0 ? &USERS : nullptr;
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
