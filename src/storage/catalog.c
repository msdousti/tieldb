/**
 * The catalog: every table, found by name.
 */

// Read-write locks are declared only when POSIX features are asked for
#define _POSIX_C_SOURCE 200809L

#include "storage/catalog.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>

static constexpr size_t INITIAL_BUCKETS = 64;

static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;
static hash_map_t *tables; // created with the first table

static size_t hash_name(const void *name)
{
    return hash_bytes(name, strlen(name));
}

static bool has_name(const hash_node_t *node, const void *name)
{
    return strcmp(table_name(table_from_node(node)), name) == 0;
}

static const hash_map_ops_t TABLE_OPS = {.hash = hash_name, .has_key = has_name};

static table_t *find_locked(const char *name)
{
    return tables == nullptr ? nullptr : table_from_node(hash_map_find(tables, name));
}

static table_t *create_locked(const char *name, const table_column_t *columns, size_t count)
{
    if (find_locked(name) != nullptr) {
        errno = EEXIST;
        return nullptr;
    }
    if (tables == nullptr) {
        tables = hash_map_create(&TABLE_OPS, INITIAL_BUCKETS);
    }
    table_t *table = tables == nullptr ? nullptr : table_create(name, columns, count);
    if (table != nullptr) {
        hash_map_insert(tables, table_node(table), table_name(table));
    }
    return table;
}

table_t *catalog_find(const char *name)
{
    pthread_rwlock_rdlock(&lock);
    table_t *table = find_locked(name);
    pthread_rwlock_unlock(&lock);
    return table;
}

table_t *catalog_create_table(const char *name, const table_column_t *columns, size_t column_count)
{
    pthread_rwlock_wrlock(&lock);
    table_t *table = create_locked(name, columns, column_count);
    pthread_rwlock_unlock(&lock);
    return table;
}
