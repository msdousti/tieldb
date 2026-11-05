#include "ds/max_tree.h"

#include <errno.h>
#include <stdbit.h>
#include <string.h>

static size_t left_child(size_t node_index)
{
    return (2 * node_index) + 1;
}

static size_t parent_of(size_t node_index)
{
    return (node_index - 1) / 2;
}

static size_t stored_node_count(const max_tree_t *tree)
{
    return tree->first_leaf + tree->leaf_count;
}

// The padded leaves are not stored; they read as 0
static uint8_t value_at(const max_tree_t *tree, size_t node_index)
{
    if (node_index >= stored_node_count(tree)) {
        return 0;
    }
    return tree->nodes[node_index];
}

static uint8_t larger_child(const max_tree_t *tree, size_t node_index)
{
    uint8_t left_value = value_at(tree, left_child(node_index));
    uint8_t right_value = value_at(tree, left_child(node_index) + 1);

    return left_value > right_value ? left_value : right_value;
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
    // The inner nodes (padded - 1) and the real leaves: at most 2^64 - 1, so the sum cannot wrap
    *bytes = (padded - 1) + leaf_count;
    return 0;
}

static int bind_memory(max_tree_t *tree, void *memory, size_t memory_size, size_t leaf_count)
{
    size_t bytes = 0;

    if (max_tree_size(leaf_count, &bytes) != 0) {
        return -1;
    }
    if (memory == nullptr || memory_size < bytes) {
        errno = EINVAL;
        return -1;
    }
    tree->nodes = memory;
    tree->leaf_count = leaf_count;
    tree->first_leaf = stdc_bit_ceil(leaf_count) - 1;
    return 0;
}

int max_tree_init(max_tree_t *tree, void *memory, size_t memory_size, size_t leaf_count)
{
    if (bind_memory(tree, memory, memory_size, leaf_count) != 0) {
        return -1;
    }
    memset(memory, 0, stored_node_count(tree));
    return 0;
}

int max_tree_attach(max_tree_t *tree, void *memory, size_t memory_size, size_t leaf_count)
{
    if (bind_memory(tree, memory, memory_size, leaf_count) != 0) {
        return -1;
    }
    for (size_t node_index = tree->first_leaf; node_index > 0; node_index--) {
        tree->nodes[node_index - 1] = larger_child(tree, node_index - 1);
    }
    return 0;
}

static bool refresh_inner(max_tree_t *tree, size_t node_index)
{
    uint8_t maximum = larger_child(tree, node_index);

    if (tree->nodes[node_index] == maximum) {
        return false;
    }
    tree->nodes[node_index] = maximum;
    return true;
}

static void recompute_parents(max_tree_t *tree, size_t node_index)
{
    while (node_index > 0 && refresh_inner(tree, parent_of(node_index))) {
        node_index = parent_of(node_index);
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
    size_t node_index = 0;
    while (node_index < tree->first_leaf) {
        size_t left_index = left_child(node_index);
        node_index = value_at(tree, left_index) >= at_least ? left_index : left_index + 1;
    }
    *leaf = node_index - tree->first_leaf;
    return 0;
}
