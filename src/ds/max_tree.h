/**
 * A binary max-tree over byte values, kept in memory that the caller owns.
 *
 * The leaves are numbered from 0 and hold one byte each. Every inner node holds the maximum of
 * its two children, so the root is the maximum of all leaves. A leaf changes in place, and its
 * parents are recomputed up to the root; the update stops as soon as a parent keeps its value.
 * The search finds the leftmost leaf whose value is at least a given number, by walking down from
 * the root, so it costs the height of the tree whatever the number of leaves.
 *
 * The tree is stored as an array, the node at index i having its children at 2i+1 and 2i+2. A
 * leaf count that is not a power of two is padded with leaves that hold 0 and are never set, so
 * a search for 0 or more still returns a real leaf.
 *
 * The module allocates nothing: max_tree_init is given the bytes, which can be the body of a
 * page. A max_tree_t is only a view of them, so it needs no destroy function, and copies of it
 * share the bytes. It does no locking.
 *
 * Errors return -1 and set errno, as documented for each function.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *nodes;
    size_t leaf_count;
    size_t first_leaf;
} max_tree_t;

/**
 * The number of bytes that max_tree_init needs for leaf_count leaves.
 * EINVAL: no leaves. EOVERFLOW: the size does not fit in a size_t.
 */
[[nodiscard]] int max_tree_size(size_t leaf_count, size_t *bytes);

/**
 * Makes tree a view of memory, with leaf_count leaves that all hold 0.
 * EINVAL: no leaves, a null memory pointer, or memory_size smaller than max_tree_size says.
 * EOVERFLOW: as for max_tree_size.
 */
[[nodiscard]] int max_tree_init(max_tree_t *tree, void *memory, size_t memory_size,
                                size_t leaf_count);

/** Sets a leaf and recomputes its parents. EINVAL: the leaf does not exist. */
[[nodiscard]] int max_tree_set(max_tree_t *tree, size_t leaf, uint8_t value);

/** Reads a leaf into value. EINVAL: the leaf does not exist. */
[[nodiscard]] int max_tree_get(const max_tree_t *tree, size_t leaf, uint8_t *value);

/** The maximum of all leaves. */
[[nodiscard]] uint8_t max_tree_root(const max_tree_t *tree);

/** Puts the leftmost leaf whose value is at least at_least into leaf. ENOENT: there is none. */
[[nodiscard]] int max_tree_find(const max_tree_t *tree, uint8_t at_least, size_t *leaf);
