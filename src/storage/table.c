/**
 * Tables: the catalog entry of a table, with a storage engine instance that holds its rows.
 */

#include "storage/table.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "storage/one_page_engine.h"

typedef struct {
    table_column_t column;
    size_t display_position;
} column_slot_t;

struct table {
    hash_node_t node; // for the catalog's hash map
    const char *name;
    size_t column_count;
    storage_engine_t *engine;
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

static storage_engine_t *create_engine(const table_column_t *columns, size_t column_count)
{
    const type_info_t *types[TABLE_MAX_COLUMNS];
    for (size_t i = 0; i < column_count; i++) {
        types[i] = columns[i].type;
    }
    return one_page_engine_create(&(row_layout_t){types, column_count});
}

table_t *table_create(const char *name, const table_column_t *columns, size_t column_count)
{
    table_t *table = calloc(1, sizeof(*table) + owned_size(name, columns, column_count));
    if (table == nullptr) {
        return nullptr;
    }
    table->engine = create_engine(columns, column_count);
    if (table->engine == nullptr) {
        free(table);
        return nullptr;
    }
    table->column_count = column_count;
    table->lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    fill_owned(table, name, columns);
    return table;
}

void table_destroy(table_t *table)
{
    storage_destroy(table->engine);
    pthread_mutex_destroy(&table->lock);
    free(table);
}

static size_t stored_capacity(const type_info_t *type, const char *text)
{
    return type->storage_size == TYPE_VARIABLE_LENGTH ? strlen(text) + 1 : type->storage_size;
}

static size_t total_capacity(const table_t *table, const char *const *cells)
{
    size_t total = 0;
    for (size_t i = 0; i < table->column_count; i++) {
        total += stored_capacity(table->columns[i].column.type, cells[i]);
    }
    return total;
}

static int convert_cell(const type_info_t *type, const char *text, unsigned char *buffer,
                        row_value_t *value)
{
    ssize_t length = type->input(text, buffer, stored_capacity(type, text));
    if (length < 0) {
        return -1;
    }
    *value = (row_value_t){buffer, (size_t) length};
    return 0;
}

static int convert_cells(const table_t *table, const char *const *cells, unsigned char *buffer,
                         row_value_t *values)
{
    int status = 0;
    for (size_t i = 0; i < table->column_count && status == 0; i++) {
        const type_info_t *type = table->columns[i].column.type;
        status = convert_cell(type, cells[i], buffer, &values[i]);
        buffer += stored_capacity(type, cells[i]);
    }
    return status;
}

static int insert_converted(table_t *table, const char *const *cells)
{
    row_value_t values[TABLE_MAX_COLUMNS];
    unsigned char *buffer = malloc(total_capacity(table, cells) + 1); // never zero
    if (buffer == nullptr) {
        return -1;
    }
    int status = convert_cells(table, cells, buffer, values);
    if (status == 0) {
        status = storage_insert(table->engine, values);
    }
    free(buffer);
    return status;
}

int table_insert_locked(table_t *table, const char *const *cells, size_t count)
{
    if (table == nullptr || cells == nullptr || count != table->column_count) {
        errno = EINVAL;
        return -1;
    }
    return insert_converted(table, cells);
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

hash_node_t *table_node(table_t *table)
{
    return &table->node;
}

table_t *table_from_node(const hash_node_t *node)
{
    return HASH_MAP_ENTRY(node, table_t, node);
}

size_t table_column_count(const table_t *table)
{
    return table->column_count;
}

const table_column_t *table_column(const table_t *table, size_t index)
{
    return &table->columns[index].column;
}

storage_engine_t *table_engine(const table_t *table)
{
    return table->engine;
}

int table_save_rows(table_t *table, const char *path)
{
    table_lock(table);
    int status = storage_save(table->engine, path);
    table_unlock(table);
    return status;
}
