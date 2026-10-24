/**
 * Tests for the hash map
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ds/hash_map.h"
#include "test_random.h"

typedef struct {
    hash_node_t node;
    uint32_t key;
    uint32_t value;
} entry_t;

static size_t hash_key(const void *key)
{
    return hash_bytes(key, sizeof(uint32_t));
}

// Every key lands in the same chain
static size_t hash_to_one_chain([[maybe_unused]] const void *key)
{
    return 7;
}

static bool has_key(const hash_node_t *node, const void *key)
{
    const entry_t *entry = HASH_MAP_ENTRY(node, const entry_t, node);
    return entry->key == *(const uint32_t *) key;
}

static const hash_map_ops_t OPS = {.hash = hash_key, .has_key = has_key};
static const hash_map_ops_t ONE_CHAIN_OPS = {.hash = hash_to_one_chain, .has_key = has_key};

static hash_map_t *create_map(const hash_map_ops_t *ops, size_t buckets)
{
    hash_map_t *map = hash_map_create(ops, buckets);
    if (map == nullptr) {
        fail();
    }
    return map;
}

static entry_t *find_entry(const hash_map_t *map, uint32_t key)
{
    return HASH_MAP_ENTRY(hash_map_find(map, &key), entry_t, node);
}

static void test_hash_bytes_matches_known_fnv_1a_values([[maybe_unused]] void **state)
{
    assert_true(hash_bytes("", 0) == 0xcbf29ce484222325);
    assert_true(hash_bytes("a", 1) == 0xaf63dc4c8601ec8c);
    assert_true(hash_bytes("foobar", 6) == 0x85944171f73967e8);
}

static void test_empty_map_finds_nothing([[maybe_unused]] void **state)
{
    hash_map_t *map = create_map(&OPS, 0);
    uint32_t key = 1;

    assert_int_equal(hash_map_count(map), 0);
    assert_null(hash_map_find(map, &key));
    assert_null(hash_map_remove(map, &key));
    hash_map_destroy(map);
}

static void test_inserted_entries_are_found_and_removed([[maybe_unused]] void **state)
{
    hash_map_t *map = create_map(&OPS, 8);
    entry_t first = {.key = 1, .value = 10};
    entry_t second = {.key = 2, .value = 20};
    uint32_t key = 1;

    hash_map_insert(map, &first.node, &first.key);
    hash_map_insert(map, &second.node, &second.key);

    assert_int_equal(hash_map_count(map), 2);
    assert_ptr_equal(find_entry(map, 1), &first);
    assert_ptr_equal(find_entry(map, 2), &second);
    assert_ptr_equal(HASH_MAP_ENTRY(hash_map_remove(map, &key), entry_t, node), &first);
    assert_null(find_entry(map, 1));
    assert_ptr_equal(find_entry(map, 2), &second);
    assert_int_equal(hash_map_count(map), 1);
    hash_map_destroy(map);
}

static void count_visit(hash_node_t *node, void *context)
{
    size_t *sum = context;
    *sum += HASH_MAP_ENTRY(node, entry_t, node)->key;
}

static size_t sum_of_keys_visited(const hash_map_ops_t *ops)
{
    entry_t entries[5];
    hash_map_t *map = create_map(ops, 8);
    size_t sum = 0;

    for (uint32_t i = 0; i < 5; i++) {
        entries[i] = (entry_t){.key = i + 1};
        hash_map_insert(map, &entries[i].node, &entries[i].key);
    }
    hash_map_for_each(map, count_visit, &sum);
    hash_map_destroy(map);
    return sum;
}

static void test_for_each_visits_every_entry_once([[maybe_unused]] void **state)
{
    assert_int_equal(sum_of_keys_visited(&OPS), 1 + 2 + 3 + 4 + 5);
    assert_int_equal(sum_of_keys_visited(&ONE_CHAIN_OPS), 1 + 2 + 3 + 4 + 5);
}

static void test_create_rejects_missing_operations([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(hash_map_create(nullptr, 8));
    assert_int_equal(errno, EINVAL);
}

static constexpr uint32_t KEY_RANGE = 300;
static constexpr size_t OPERATIONS = 20000;

// The model is a plain array of flags next to the map
typedef struct {
    hash_map_t *map;
    entry_t entries[KEY_RANGE];
    bool present[KEY_RANGE];
    size_t count;
} model_t;

static void model_insert(model_t *model, uint32_t key, size_t step)
{
    model->entries[key] = (entry_t){.key = key, .value = (uint32_t) step};
    hash_map_insert(model->map, &model->entries[key].node, &model->entries[key].key);
    model->present[key] = true;
    model->count++;
}

static void model_remove(model_t *model, uint32_t key)
{
    hash_node_t *removed = hash_map_remove(model->map, &key);

    assert_ptr_equal(HASH_MAP_ENTRY(removed, entry_t, node), &model->entries[key]);
    model->present[key] = false;
    model->count--;
}

static void model_step(model_t *model, uint64_t *random, size_t step)
{
    uint32_t key = (uint32_t) (next_random(random) % KEY_RANGE);
    uint64_t action = next_random(random) % 3;

    if (action == 0 && !model->present[key]) {
        model_insert(model, key, step);
    }
    if (action == 1 && model->present[key]) {
        model_remove(model, key);
    }
    assert_true((find_entry(model->map, key) != nullptr) == model->present[key]);
}

// Random inserts, removes and lookups on the map and on the model, which must always agree
static void run_against_model(const hash_map_ops_t *ops, size_t buckets, uint64_t seed)
{
    static model_t model;
    uint64_t random = seed;

    model = (model_t){.map = create_map(ops, buckets)};
    for (size_t step = 0; step < OPERATIONS; step++) {
        model_step(&model, &random, step);
    }
    assert_int_equal(hash_map_count(model.map), model.count);
    hash_map_destroy(model.map);
}

static void test_map_agrees_with_a_model_under_random_operations([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261018};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        print_message("seed %llu\n", (unsigned long long) seeds[i]);
        run_against_model(&OPS, 8, seeds[i]);
        run_against_model(&OPS, 100, seeds[i]);
        run_against_model(&ONE_CHAIN_OPS, 8, seeds[i]);
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_hash_bytes_matches_known_fnv_1a_values),
    cmocka_unit_test(test_empty_map_finds_nothing),
    cmocka_unit_test(test_inserted_entries_are_found_and_removed),
    cmocka_unit_test(test_for_each_visits_every_entry_once),
    cmocka_unit_test(test_create_rejects_missing_operations),
    cmocka_unit_test(test_map_agrees_with_a_model_under_random_operations),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
