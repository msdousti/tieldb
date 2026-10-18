/**
 * In-memory tables that can grow.
 */

#include "storage/table.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t INITIAL_CELL_CAPACITY = 16;
static constexpr char USER_TABLE_NAME[] = "user";
static constexpr size_t USER_COLUMN_COUNT = 2;
static constexpr size_t USER_ROW_COUNT = 3;

struct table {
    const char *name;
    const table_column_t *columns;
    size_t column_count;
    char **cells;
    size_t cell_count;
    size_t cell_capacity;
    pthread_mutex_t lock;
};

table_t *table_create(const char *name, const table_column_t *columns, size_t column_count)
{
    table_t *table = calloc(1, sizeof(*table));
    if (table == nullptr) {
        return nullptr;
    }
    table->name = name;
    table->columns = columns;
    table->column_count = column_count;
    table->lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
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
    return status;
}

int table_insert(table_t *table, const char *const *cells, size_t count)
{
    if (table == nullptr || cells == nullptr || count != table->column_count) {
        errno = EINVAL;
        return -1;
    }
    table_lock(table);
    int status = append_row(table, cells, count);
    table_unlock(table);
    return status;
}

void table_lock(table_t *table)
{
    pthread_mutex_lock(&table->lock);
}

void table_unlock(table_t *table)
{
    pthread_mutex_unlock(&table->lock);
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
    return table->cell_count / table->column_count;
}

const char *table_cell(const table_t *table, size_t row, size_t column)
{
    return table->cells[(row * table->column_count) + column];
}

static const table_column_t USER_COLUMNS[USER_COLUMN_COUNT] = {
    {"id", COLUMN_TYPE_INTEGER},
    {"name", COLUMN_TYPE_TEXT},
};

static const char *const USER_ROWS[USER_ROW_COUNT][USER_COLUMN_COUNT] = {
    {"1", "Alice"},
    {"2", "Bob"},
    {"3", "Carol"},
};

static pthread_once_t user_table_once = PTHREAD_ONCE_INIT;
static table_t *user_table;

// An allocation failure this early would leave the process unusable anyway, so the inserts are
// not checked; table_find then reports a table that is missing or has fewer rows
static void create_user_table(void)
{
    user_table = table_create(USER_TABLE_NAME, USER_COLUMNS, USER_COLUMN_COUNT);
    for (size_t i = 0; i < USER_ROW_COUNT; i++) {
        (void) table_insert(user_table, USER_ROWS[i], USER_COLUMN_COUNT);
    }
}

table_t *table_find(const char *name)
{
    pthread_once(&user_table_once, create_user_table);
    return strcmp(name, USER_TABLE_NAME) == 0 ? user_table : nullptr;
}
