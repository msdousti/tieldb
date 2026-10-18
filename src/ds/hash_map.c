/**
 * A hash map with chaining, for entries that the caller allocates and owns.
 */

#include "ds/hash_map.h"

#include <errno.h>
#include <stdlib.h>

static constexpr size_t MIN_BUCKETS = 8;
static constexpr size_t LOAD_FACTOR_NUMERATOR = 3;
static constexpr size_t LOAD_FACTOR_DENOMINATOR = 4;
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;
static constexpr uint64_t FNV_PRIME = 0x100000001b3;

static_assert(sizeof(size_t) == sizeof(uint64_t));

struct hash_map {
    const hash_map_ops_t *ops;
    hash_node_t **buckets;
    size_t bucket_count;
    size_t count;
};

static size_t round_up_buckets(size_t wanted)
{
    size_t buckets = MIN_BUCKETS;
    while (buckets < wanted) {
        buckets *= 2;
    }
    return buckets;
}

hash_map_t *hash_map_create(const hash_map_ops_t *ops, size_t initial_buckets)
{
    if (ops == nullptr) {
        errno = EINVAL;
        return nullptr;
    }
    hash_map_t *map = calloc(1, sizeof(*map));
    if (map == nullptr) {
        return nullptr;
    }
    map->ops = ops;
    map->bucket_count = round_up_buckets(initial_buckets);
    map->buckets = (hash_node_t **) calloc(map->bucket_count, sizeof(*map->buckets));
    if (map->buckets == nullptr) {
        free(map);
        return nullptr;
    }
    return map;
}

void hash_map_destroy(hash_map_t *map)
{
    free((void *) map->buckets);
    free(map);
}

static hash_node_t **bucket_for(const hash_map_t *map, size_t hash)
{
    return &map->buckets[hash & (map->bucket_count - 1)];
}

static void link_node(hash_map_t *map, hash_node_t *node)
{
    hash_node_t **bucket = bucket_for(map, node->hash);
    node->next = *bucket;
    *bucket = node;
}

static void relink_chain(hash_map_t *map, hash_node_t *node)
{
    while (node != nullptr) {
        hash_node_t *next = node->next;
        link_node(map, node);
        node = next;
    }
}

// Doubles the buckets and relinks the entries. If memory runs out, the map is left as it was.
static void grow(hash_map_t *map)
{
    hash_node_t **old_buckets = map->buckets;
    size_t old_count = map->bucket_count;
    hash_node_t **grown = (hash_node_t **) calloc(old_count * 2, sizeof(*grown));
    if (grown == nullptr) {
        return;
    }
    map->buckets = grown;
    map->bucket_count = old_count * 2;
    for (size_t i = 0; i < old_count; i++) {
        relink_chain(map, old_buckets[i]);
    }
    free((void *) old_buckets);
}

static bool is_overloaded(const hash_map_t *map)
{
    return map->count * LOAD_FACTOR_DENOMINATOR > map->bucket_count * LOAD_FACTOR_NUMERATOR;
}

void hash_map_insert(hash_map_t *map, hash_node_t *node, const void *key)
{
    node->hash = map->ops->hash(key);
    link_node(map, node);
    map->count++;
    if (is_overloaded(map)) {
        grow(map);
    }
}

hash_node_t *hash_map_find(const hash_map_t *map, const void *key)
{
    size_t hash = map->ops->hash(key);
    hash_node_t *node = *bucket_for(map, hash);
    while (node != nullptr && !(node->hash == hash && map->ops->has_key(node, key))) {
        node = node->next;
    }
    return node;
}

hash_node_t *hash_map_remove(hash_map_t *map, const void *key)
{
    size_t hash = map->ops->hash(key);
    hash_node_t **link = bucket_for(map, hash);
    while (*link != nullptr && !((*link)->hash == hash && map->ops->has_key(*link, key))) {
        link = &(*link)->next;
    }
    hash_node_t *removed = *link;
    if (removed != nullptr) {
        *link = removed->next;
        map->count--;
    }
    return removed;
}

size_t hash_map_count(const hash_map_t *map)
{
    return map->count;
}

size_t hash_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * FNV_PRIME;
    }
    return (size_t) hash;
}
