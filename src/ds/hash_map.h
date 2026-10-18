/**
 * A hash map with chaining, for entries that the caller allocates and owns.
 *
 * The map does not allocate entries. Each entry embeds a hash_node_t, which the map links into
 * its buckets, and HASH_MAP_ENTRY turns a node pointer back into the entry:
 *
 *   typedef struct { hash_node_t node; int id; ... } page_t;
 *   page_t *page = HASH_MAP_ENTRY(hash_map_find(map, &id), page_t, node);
 *
 * What the key is, how it is hashed and when an entry has a given key are up to the caller, through
 * hash_map_ops_t, so any key type works. Entries never move while they are in the map, so
 * pointers to them stay valid; they stop being valid when the caller frees them.
 *
 * Insert never fails: if the map runs out of memory while growing, it keeps its buckets and the
 * chains get longer. The map has no locks; callers wrap it in the locking that suits them.
 *
 * Errors return nullptr and set errno: EINVAL for a null argument, ENOMEM when memory runs out
 * in hash_map_create.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** The link that an entry embeds. Its fields belong to the map. */
typedef struct hash_node {
    struct hash_node *next;
    size_t hash;
} hash_node_t;

/** The address of the entry that contains a node; nullptr for a null node. */
static inline void *hash_node_entry(const hash_node_t *node, size_t offset)
{
    return node == nullptr ? nullptr : (void *) ((const char *) node - offset);
}

/**
 * Turns a pointer to the embedded node back into a pointer to the entry that contains it (or
 * nullptr, for a null node). The node expression is evaluated once.
 */
#define HASH_MAP_ENTRY(node, type, member) \
    ((type *) hash_node_entry((node), offsetof(type, member)))

typedef struct {
    size_t (*hash)(const void *key);
    bool (*has_key)(const hash_node_t *entry, const void *key);
} hash_map_ops_t;

typedef struct hash_map hash_map_t;

/** The map starts with initial_buckets buckets, rounded up to a power of two (at least 8). */
[[nodiscard]] hash_map_t *hash_map_create(const hash_map_ops_t *ops, size_t initial_buckets);

/** Does not free the entries; the caller owns them. */
void hash_map_destroy(hash_map_t *map);

/** Adds an entry. The key must not be in the map yet (call hash_map_find first if unsure). */
void hash_map_insert(hash_map_t *map, hash_node_t *node, const void *key);

/** Returns the entry with that key, or nullptr. */
[[nodiscard]] hash_node_t *hash_map_find(const hash_map_t *map, const void *key);

/** Unlinks the entry with that key and returns it, or returns nullptr. */
hash_node_t *hash_map_remove(hash_map_t *map, const void *key);

[[nodiscard]] size_t hash_map_count(const hash_map_t *map);

/** FNV-1a hash of some bytes, for keys that are strings or plain data. */
[[nodiscard]] size_t hash_bytes(const void *data, size_t size);
