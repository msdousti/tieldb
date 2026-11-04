#include "ds/max_tree.h"

#include <errno.h>
#include <stdbit.h>
#include <stdckdint.h>
#include <string.h>

static size_t left_child(size_t node)
{
    return (2 * node) + 1;
}

static size_t parent_of(size_t node)
{
    return (node - 1) / 2;
}

static uint8_t larger_child(const max_tree_t *tree, size_t node)
{
    uint8_t left = tree->nodes[left_child(node)];
    uint8_t right = tree->nodes[left_child(node) + 1];

    return left > right ? left : right;
}

static int padded_leaf_count(size_t leaf_count, size_t *padded)
{
    if (leaf_count == 0) {
        errno = EINVAL;
        return -1;
    }
    *padded = stdc_bit_ceil(leaf_count);
    if (*padded == 0) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

int max_tree_size(size_t leaf_count, size_t *bytes)
{
    size_t padded = 0;

    if (padded_leaf_count(leaf_count, &padded) != 0) {
        return -1;
    }
    if (ckd_mul(bytes, padded, 2)) {
        errno = EOVERFLOW;
        return -1;
    }
    *bytes -= 1;
    return 0;
}

int max_tree_init(max_tree_t *tree, void *memory, size_t memory_size, size_t leaf_count)
{
    size_t bytes = 0;

    if (max_tree_size(leaf_count, &bytes) != 0) {
        return -1;
    }
    if (memory == nullptr || memory_size < bytes) {
        errno = EINVAL;
        return -1;
    }
    memset(memory, 0, bytes);
    tree->nodes = memory;
    tree->leaf_count = leaf_count;
    tree->first_leaf = stdc_bit_ceil(leaf_count) - 1;
    return 0;
}

static bool refresh_node(max_tree_t *tree, size_t node)
{
    uint8_t maximum = larger_child(tree, node);

    if (tree->nodes[node] == maximum) {
        return false;
    }
    tree->nodes[node] = maximum;
    return true;
}

static void recompute_parents(max_tree_t *tree, size_t node)
{
    while (node > 0 && refresh_node(tree, parent_of(node))) {
        node = parent_of(node);
    }
}

int max_tree_set(max_tree_t *tree, size_t leaf, uint8_t value)
{
    if (leaf >= tree->leaf_count) {
        errno = EINVAL;
        return -1;
    }
    tree->nodes[tree->first_leaf + leaf] = value;
    recompute_parents(tree, tree->first_leaf + leaf);
    return 0;
}

int max_tree_get(const max_tree_t *tree, size_t leaf, uint8_t *value)
{
    if (leaf >= tree->leaf_count) {
        errno = EINVAL;
        return -1;
    }
    *value = tree->nodes[tree->first_leaf + leaf];
    return 0;
}

uint8_t max_tree_root(const max_tree_t *tree)
{
    return tree->nodes[0];
}

int max_tree_find(const max_tree_t *tree, uint8_t at_least, size_t *leaf)
{
    if (tree->nodes[0] < at_least) {
        errno = ENOENT;
        return -1;
    }
    size_t node = 0;
    while (node < tree->first_leaf) {
        size_t left = left_child(node);
        node = tree->nodes[left] >= at_least ? left : left + 1;
    }
    *leaf = node - tree->first_leaf;
    return 0;
}
