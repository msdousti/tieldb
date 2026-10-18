/**
 * The catalog: every table, found by name.
 */

#include "storage/catalog.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t INITIAL_TABLE_CAPACITY = 8;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static table_t **tables;
static size_t table_count;
static size_t table_capacity;

static table_t *find_locked(const char *name)
{
    table_t *found = nullptr;
    for (size_t i = 0; i < table_count && found == nullptr; i++) {
        found = strcmp(table_name(tables[i]), name) == 0 ? tables[i] : nullptr;
    }
    return found;
}

static int reserve_slot_locked(void)
{
    if (table_count < table_capacity) {
        return 0;
    }
    size_t capacity = table_capacity == 0 ? INITIAL_TABLE_CAPACITY : table_capacity * 2;
    table_t **grown = (table_t **) realloc((void *) tables, capacity * sizeof(*grown));
    if (grown == nullptr) {
        return -1;
    }
    tables = grown;
    table_capacity = capacity;
    return 0;
}

static table_t *create_locked(const char *name, const table_column_t *columns, size_t count)
{
    if (find_locked(name) != nullptr) {
        errno = EEXIST;
        return nullptr;
    }
    table_t *table = table_create(name, columns, count);
    if (table == nullptr) {
        return nullptr;
    }
    if (reserve_slot_locked() != 0) {
        int saved_errno = errno;
        table_destroy(table);
        errno = saved_errno;
        return nullptr;
    }
    tables[table_count++] = table;
    return table;
}

table_t *catalog_find(const char *name)
{
    pthread_mutex_lock(&lock);
    table_t *table = find_locked(name);
    pthread_mutex_unlock(&lock);
    return table;
}

table_t *catalog_create_table(const char *name, const table_column_t *columns, size_t column_count)
{
    pthread_mutex_lock(&lock);
    table_t *table = create_locked(name, columns, column_count);
    pthread_mutex_unlock(&lock);
    return table;
}
