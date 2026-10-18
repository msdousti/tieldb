/**
 * In-memory tables that can grow.
 */

#include "storage/table.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t INITIAL_CELL_CAPACITY = 16;

typedef struct {
    table_column_t column;
    size_t display_position;
} column_slot_t;

struct table {
    const char *name;
    size_t column_count;
    char **cells;
    size_t cell_count;
    size_t cell_capacity;
    size_t row_count; // kept apart from the cells, because a table may have no columns
    pthread_mutex_t lock;
    column_slot_t columns[]; // followed in the same allocation by the table and column names
};

static size_t owned_size(const char *name, const table_column_t *columns, size_t column_count)
{
    size_t size = (column_count * sizeof(column_slot_t)) + strlen(name) + 1;
    for (size_t i = 0; i < column_count; i++) {
        size += strlen(columns[i].name) + 1;
    }
    return size;
}

static char *copy_into(char *destination, const char *text)
{
    size_t size = strlen(text) + 1;
    memcpy(destination, text, size);
    return destination + size;
}

static void fill_owned(table_t *table, const char *name, const table_column_t *columns)
{
    char *strings = (char *) &table->columns[table->column_count];
    table->name = strings;
    strings = copy_into(strings, name);
    for (size_t i = 0; i < table->column_count; i++) {
        table->columns[i] = (column_slot_t){.column = {.name = strings, .type = columns[i].type},
                                            .display_position = i};
        strings = copy_into(strings, columns[i].name);
    }
}

table_t *table_create(const char *name, const table_column_t *columns, size_t column_count)
{
    table_t *table = calloc(1, sizeof(*table) + owned_size(name, columns, column_count));
    if (table == nullptr) {
        return nullptr;
    }
    table->column_count = column_count;
    table->lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    fill_owned(table, name, columns);
    return table;
}

void table_destroy(table_t *table)
{
    for (size_t i = 0; i < table->cell_count; i++) {
        free(table->cells[i]);
    }
    free((void *) table->cells);
    pthread_mutex_destroy(&table->lock);
    free(table);
}

static int reserve_cells(table_t *table, size_t extra)
{
    size_t capacity = table->cell_capacity;
    size_t needed = table->cell_count + extra;
    while (capacity < needed) {
        capacity = capacity == 0 ? INITIAL_CELL_CAPACITY : capacity * 2;
    }
    if (capacity == table->cell_capacity) {
        return 0;
    }
    char **grown = (char **) realloc((void *) table->cells, capacity * sizeof(*grown));
    if (grown == nullptr) {
        return -1;
    }
    table->cells = grown;
    table->cell_capacity = capacity;
    return 0;
}

static int append_cell(table_t *table, const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy == nullptr) {
        return -1;
    }
    memcpy(copy, text, size);
    table->cells[table->cell_count++] = copy;
    return 0;
}

static void drop_cells_after(table_t *table, size_t count)
{
    while (table->cell_count > count) {
        free(table->cells[--table->cell_count]);
    }
}

static int append_row(table_t *table, const char *const *cells, size_t count)
{
    size_t start = table->cell_count;
    int status = reserve_cells(table, count);
    for (size_t i = 0; i < count && status == 0; i++) {
        status = append_cell(table, cells[i]);
    }
    drop_cells_after(table, status == 0 ? table->cell_count : start);
    table->row_count += status == 0 ? 1 : 0;
    return status;
}

int table_insert_locked(table_t *table, const char *const *cells, size_t count)
{
    if (table == nullptr || cells == nullptr || count != table->column_count) {
        errno = EINVAL;
        return -1;
    }
    return append_row(table, cells, count);
}

int table_insert(table_t *table, const char *const *cells, size_t count)
{
    if (table == nullptr) {
        errno = EINVAL;
        return -1;
    }
    table_lock(table);
    int status = table_insert_locked(table, cells, count);
    table_unlock(table);
    return status;
}

static bool claim_column(bool *claimed, size_t index, size_t column_count)
{
    if (index >= column_count || claimed[index]) {
        return false;
    }
    claimed[index] = true;
    return true;
}

static bool is_display_order(const size_t *order, size_t count, size_t column_count)
{
    bool claimed[TABLE_MAX_COLUMNS] = {};
    bool is_valid = count == column_count;
    for (size_t i = 0; i < count && is_valid; i++) {
        is_valid = claim_column(claimed, order[i], column_count);
    }
    return is_valid;
}

int table_set_display_order(table_t *table, const size_t *order, size_t count)
{
    if (table == nullptr || order == nullptr ||
        !is_display_order(order, count, table->column_count)) {
        errno = EINVAL;
        return -1;
    }
    table_lock(table);
    for (size_t display = 0; display < count; display++) {
        table->columns[order[display]].display_position = display;
    }
    table_unlock(table);
    return 0;
}

size_t table_physical_index(const table_t *table, size_t display_index)
{
    size_t physical = 0;
    while (table->columns[physical].display_position != display_index) {
        physical++;
    }
    return physical;
}

void table_lock(table_t *table)
{
    pthread_mutex_lock(&table->lock);
}

void table_unlock(table_t *table)
{
    pthread_mutex_unlock(&table->lock);
}

const char *table_name(const table_t *table)
{
    return table->name;
}

size_t table_column_count(const table_t *table)
{
    return table->column_count;
}

const table_column_t *table_column(const table_t *table, size_t index)
{
    return &table->columns[index].column;
}

size_t table_row_count(const table_t *table)
{
    return table->row_count;
}

const char *table_cell(const table_t *table, size_t row, size_t column)
{
    return table->cells[(row * table->column_count) + column];
}
