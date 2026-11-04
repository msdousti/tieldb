/**
 * A binary max-tree over byte values, kept in memory that the caller owns.
 *
 * The leaves are numbered from 0 and hold one byte each. Every inner node holds the maximum of
 * its two children, so the root is the maximum of all leaves. A leaf changes in place, and its
 * parents are recomputed up to the root; the update stops as soon as a parent keeps its value.
 * The search finds the leftmost leaf whose value is at least a given number, by walking down from
 * the root, so it costs the height of the tree whatever the number of leaves.
 *
 * The tree is stored as an array, the node stored at index i having its children at indices 2i+1
 * and 2i+2. A leaf count that is not a power of two is padded to one, as if with leaves that hold 0
 * and are never set, so a search for 0 or more still returns a real leaf. The padding is not
 * stored (it is the rightmost leaves, so it comes last in the array and reads as 0), which saves
 * up to half of the bytes, and that decides how many leaves fit in a page.
 *
 * The module allocates nothing: max_tree_init and max_tree_attach are given the bytes, which can
 * be the body of a page. A max_tree_t is only a view of them, so it needs no destroy function, and
 * copies of it share the bytes. It does no locking.
 *
 * Errors return -1 and set errno, as documented for each function.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * A handle to a tree, not the tree: the nodes are the bytes it points to, one byte per node, with
 * no pointers between them, because a pointer stored in a page means nothing after the page is
 * read back, and a link would be larger than the value. The children and the parent of a node are
 * computed from its index in the array. Only the nodes belong in a page; the other two fields
 * follow from the leaf count. max_tree_init makes an empty tree, and max_tree_attach makes a
 * handle for bytes that already hold leaves (a page read back).
 *
 * nodes: the caller's bytes; the root is nodes[0] and the leaves are the last of them.
 * leaf_count: the number of real leaves.
 * first_leaf: the index in nodes of leaf 0 (the padded leaf count minus 1), kept so that no call
 * has to compute it again.
 *
 * The type is public on purpose, an exception to "stateful types are opaque": it holds no
 * resources, so there is nothing to create or destroy, and a caller can keep it on the stack or
 * inside another struct. The fields are private; use only the functions. leaf_count and first_leaf
 * are set by max_tree_init or max_tree_attach and never change afterwards.
 */
typedef struct {
    uint8_t *nodes;
    size_t leaf_count;
    size_t first_leaf;
} max_tree_t;

/**
 * The number of bytes that max_tree_init and max_tree_attach need for leaf_count leaves: the
 * inner nodes of the padded tree and the real leaves.
 * EINVAL: no leaves. EOVERFLOW: the size does not fit in a size_t.
 */
[[nodiscard]] int max_tree_size(size_t leaf_count, size_t *bytes);

/**
 * Makes tree a view of memory, with leaf_count leaves that all hold 0. Overwrites the first
 * max_tree_size(leaf_count) bytes of memory with zeros, so anything the memory held is lost; the
 * bytes after those are not touched. Not for bytes that hold a tree to keep: see max_tree_attach.
 * EINVAL: no leaves, a null memory pointer, or memory_size smaller than max_tree_size says.
 * EOVERFLOW: as for max_tree_size.
 */
[[nodiscard]] int max_tree_init(max_tree_t *tree, void *memory, size_t memory_size,
                                size_t leaf_count);

/**
 * Makes tree a view of memory that already holds leaf_count leaves, such as a page read back. The
 * leaf bytes are taken as they are (any byte is a valid leaf value) and every inner node is
 * recomputed from them, so memory in any state, torn or damaged, becomes a valid tree and the
 * result cannot be wrong; only the leaves, which this function cannot check, may be. Reads and
 * writes the first max_tree_size(leaf_count) bytes of memory and no others.
 * Errors: as for max_tree_init.
 */
[[nodiscard]] int max_tree_attach(max_tree_t *tree, void *memory, size_t memory_size,
                                  size_t leaf_count);

/** Sets a leaf and recomputes its parents. EINVAL: the leaf does not exist. */
[[nodiscard]] int max_tree_set(max_tree_t *tree, size_t leaf, uint8_t value);

/** Reads a leaf into value. EINVAL: the leaf does not exist. */
[[nodiscard]] int max_tree_get(const max_tree_t *tree, size_t leaf, uint8_t *value);

/** The maximum of all leaves. */
[[nodiscard]] uint8_t max_tree_root(const max_tree_t *tree);

/** Puts the leftmost leaf whose value is at least at_least into leaf. ENOENT: there is none. */
[[nodiscard]] int max_tree_find(const max_tree_t *tree, uint8_t at_least, size_t *leaf);
