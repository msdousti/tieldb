#define _POSIX_C_SOURCE 200809L

/**
 * Tests for the free space map
 *
 * Each test has a directory with the map's page file and a buffer pool of two frames. The map is
 * compared with a plain array of hints.
 */

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <cmocka.h>

#include "buffer/buffer_pool.h"
#include "ds/clock_policy.h"
#include "io_fault.h"
#include "storage/free_space_map.h"
#include "storage/fsm_page.h"
#include "storage/slotted_page.h"
#include "test_directory.h"
#include "test_random.h"

static constexpr size_t FRAMES = 2;
static constexpr size_t CATEGORY = FREE_SPACE_MAP_CATEGORY_SIZE;
static constexpr size_t MOST_FREE = PAGE_SIZE - PAGE_HEADER_SIZE;

static char directory[] = "/tmp/tieldb_free_space_map_XXXXXX";
static char map_path[128];
static char other_path[128];
static page_file_t *map_file;
static buffer_pool_t *pool;
static free_space_map_t *map;

static buffer_pool_t *new_pool(void)
{
    buffer_pool_t *created = buffer_pool_create(FRAMES, clock_policy_create(FRAMES));
    assert_true(created != nullptr);
    return created;
}

static void open_map(void)
{
    pool = new_pool();
    map = free_space_map_create(pool, map_file);
    assert_true(map != nullptr);
}

static void close_map(void)
{
    free_space_map_destroy(map);
    assert_int_equal(buffer_pool_destroy(pool), 0);
    map = nullptr;
    pool = nullptr;
}

static void restart(void)
{
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    close_map();
    open_map();
}

static int setup([[maybe_unused]] void **state)
{
    io_init(true);
    make_directory(directory);
    snprintf(map_path, sizeof(map_path), "%s/map", directory);
    snprintf(other_path, sizeof(other_path), "%s/other", directory);
    map_file = page_file_create(map_path, PAGE_FILE_NO_EXTRA_FLAGS);
    assert_true(map_file != nullptr);
    open_map();
    return 0;
}

static int teardown([[maybe_unused]] void **state)
{
    io_init(true);
    close_map();
    assert_int_equal(page_file_close(map_file), 0);
    remove_directory(directory);
    strcpy(directory, "/tmp/tieldb_free_space_map_XXXXXX");
    return 0;
}

static void assert_found(size_t needed, size_t expected)
{
    size_t heap_page = SIZE_MAX;

    assert_int_equal(free_space_map_find(map, needed, &heap_page), 0);
    assert_int_equal(heap_page, expected);
}

static void assert_not_found(size_t needed)
{
    size_t heap_page = SIZE_MAX;

    errno = 0;
    assert_int_equal(free_space_map_find(map, needed, &heap_page), -1);
    assert_int_equal(errno, ENOENT);
    assert_int_equal(heap_page, SIZE_MAX);
}

static void record(size_t heap_page, size_t free_bytes)
{
    assert_int_equal(free_space_map_record(map, heap_page, free_bytes), 0);
}

static void test_a_new_map_knows_no_page([[maybe_unused]] void **state)
{
    assert_not_found(1);
    assert_not_found(CATEGORY);
    assert_not_found(FREE_SPACE_MAP_LARGEST_REQUEST);
}

static void test_a_record_rounds_down_and_a_search_rounds_up([[maybe_unused]] void **state)
{
    record(5, 100);

    assert_found(0, 0);
    assert_found(1, 5);
    assert_found(3 * CATEGORY, 5);
    assert_not_found((3 * CATEGORY) + 1);
    assert_not_found(100);
}

static void
test_the_search_returns_the_first_page_that_is_large_enough([[maybe_unused]] void **state)
{
    record(7, 500);
    record(3, 500);
    record(9, 5000);

    assert_found(500 - CATEGORY, 3);
    assert_found(1000, 9);
    record(3, 0);
    assert_found(500 - CATEGORY, 7);
    record(7, 0);
    record(9, 0);
    assert_not_found(1);
}

static void test_the_largest_values_are_accepted([[maybe_unused]] void **state)
{
    record(FSM_PAGE_LEAVES - 1, MOST_FREE);

    assert_found(FREE_SPACE_MAP_LARGEST_REQUEST, FSM_PAGE_LEAVES - 1);
    assert_not_found(FREE_SPACE_MAP_LARGEST_REQUEST + 1);
}

static void test_values_out_of_range_are_refused([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(free_space_map_record(map, FSM_PAGE_LEAVES, 100), -1);
    assert_int_equal(errno, ENOSPC);
    errno = 0;
    assert_int_equal(free_space_map_record(map, 0, MOST_FREE + 1), -1);
    assert_int_equal(errno, EINVAL);
    assert_not_found(1);
}

static void test_the_map_survives_a_restart([[maybe_unused]] void **state)
{
    record(2, 3000);
    record(40, 8000);
    restart();

    assert_found(2900, 2);
    assert_found(3100, 40);
    assert_not_found(8100);
}

static void test_nothing_stays_pinned([[maybe_unused]] void **state)
{
    size_t heap_page = 0;

    record(1, 100);
    assert_int_equal(free_space_map_find(map, 64, &heap_page), 0);
    assert_int_equal(free_space_map_find(map, 5000, &heap_page), -1);
    assert_int_equal(free_space_map_record(map, FSM_PAGE_LEAVES, 1), -1);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
}

static void corrupt_page_zero(void)
{
    unsigned char garbage[100];
    int fd = open(map_path, O_WRONLY | O_CLOEXEC);

    memset(garbage, 0xFF, sizeof(garbage));
    assert_true(fd >= 0);
    assert_int_equal(pwrite(fd, garbage, sizeof(garbage), 0), sizeof(garbage));
    assert_int_equal(close(fd), 0);
}

static void test_a_torn_page_zero_gives_an_empty_map([[maybe_unused]] void **state)
{
    page_t page;

    record(2, 3000);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    close_map();
    corrupt_page_zero();
    errno = 0;
    assert_int_equal(page_file_read(map_file, 0, &page), -1);
    assert_int_equal(errno, EBADMSG);

    open_map();
    assert_not_found(1);
    assert_int_equal(page_file_read(map_file, 0, &page), 0);
    assert_int_equal(page_type(&page), PAGE_TYPE_FSM);
    record(4, 800);
    assert_found(800, 4);
}

static page_file_t *other_file_with(page_type_t type, size_t pages)
{
    page_file_t *file = page_file_create(other_path, PAGE_FILE_NO_EXTRA_FLAGS);
    size_t number = 0;

    assert_true(file != nullptr);
    for (size_t i = 0; i < pages; i++) {
        assert_int_equal(page_file_allocate_page(file, type, &number), 0);
    }
    return file;
}

static void assert_create_fails(page_file_t *file, int error)
{
    errno = 0;
    assert_null(free_space_map_create(pool, file));
    assert_int_equal(errno, error);
}

static void test_a_file_of_another_page_type_is_refused([[maybe_unused]] void **state)
{
    page_file_t *file = other_file_with(PAGE_TYPE_HEAP, 1);

    assert_create_fails(file, EBADMSG);
    assert_int_equal(page_file_close(file), 0);
}

static void test_a_file_that_ends_in_a_partial_page_is_refused([[maybe_unused]] void **state)
{
    page_file_t *file = other_file_with(PAGE_TYPE_FSM, 1);

    assert_int_equal(truncate(other_path, PAGE_SIZE + 10), 0);
    assert_create_fails(file, EBADMSG);
    assert_int_equal(page_file_close(file), 0);
}

static void test_failing_reads_and_sizes_are_reported([[maybe_unused]] void **state)
{
    page_file_t *file = other_file_with(PAGE_TYPE_FSM, 1);

    io_init(false);
    io_inject_fault(IO_FAULT_STAT_ERROR, 1);
    assert_create_fails(file, EIO);
    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    assert_create_fails(file, EIO);
    io_init(true);
    assert_int_equal(page_file_close(file), 0);
}

static void test_a_failing_read_of_page_zero_fails_the_calls([[maybe_unused]] void **state)
{
    size_t heap_page = 0;

    io_init(false);
    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    errno = 0;
    assert_int_equal(free_space_map_find(map, 1, &heap_page), -1);
    assert_int_equal(errno, EIO);
    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    errno = 0;
    assert_int_equal(free_space_map_record(map, 1, 100), -1);
    assert_int_equal(errno, EIO);
}

// Pins and unpins every page of the other file, which pushes page 0 of the map out of the pool
static void push_page_zero_out(page_file_t *file)
{
    size_t number = 0;
    page_t *page = nullptr;

    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_int_equal(buffer_pool_add_file(pool, file, &number), 0);
    for (size_t i = 0; i < FRAMES; i++) {
        assert_int_equal(buffer_pool_pin(pool, (page_id_t){number, i}, &page), 0);
        assert_int_equal(buffer_pool_unpin(pool, (page_id_t){number, i}), 0);
    }
}

static void put_heap_page_at_page_zero(void)
{
    page_t heap;
    slotted_page_t slotted;

    slotted_page_init(&slotted, &heap, SLOTTED_PAGE_HEAP);
    assert_int_equal(page_file_write(map_file, 0, &heap), 0);
}

static void test_a_page_zero_that_changed_type_is_reported([[maybe_unused]] void **state)
{
    page_file_t *file = other_file_with(PAGE_TYPE_HEAP, FRAMES);
    size_t heap_page = 0;

    push_page_zero_out(file);
    put_heap_page_at_page_zero();
    errno = 0;
    assert_int_equal(free_space_map_find(map, 1, &heap_page), -1);
    assert_int_equal(errno, EBADMSG);
    errno = 0;
    assert_int_equal(free_space_map_record(map, 1, 100), -1);
    assert_int_equal(errno, EBADMSG);
    assert_int_equal(page_file_close(file), 0);
}

// The map is the first file added to its pool, so its file number is 0
static constexpr size_t MAP_FILE_NUMBER = 0;
static constexpr size_t INNER_NODES = 4095;
static constexpr unsigned char TOO_HIGH = 200;

static void make_inner_nodes_promise_too_much(void)
{
    page_id_t id = {.file_number = MAP_FILE_NUMBER, .page_number = 0};
    page_t *page = nullptr;
    fsm_page_t fsm;
    size_t length = 0;

    assert_int_equal(buffer_pool_pin(pool, id, &page), 0);
    assert_int_equal(fsm_page_open(&fsm, page), 0);
    memset(fsm_page_body(&fsm, &length), TOO_HIGH, INNER_NODES);
    assert_int_equal(buffer_pool_mark_changed(pool, id), 0);
    assert_int_equal(buffer_pool_unpin(pool, id), 0);
}

static void test_a_search_repairs_inner_nodes_that_promise_too_much([[maybe_unused]] void **state)
{
    record(5, 100);

    make_inner_nodes_promise_too_much();
    assert_found(64, 5);
    make_inner_nodes_promise_too_much();
    assert_not_found(1000);
    restart();
    assert_found(96, 5);
    assert_not_found(97);
}

static constexpr size_t MODEL_PAGES = 120;
static constexpr size_t MODEL_STEPS = 3000;
static constexpr size_t RESTART_EVERY = 400;

static size_t model_find(const uint8_t *categories, size_t needed)
{
    size_t wanted = (needed + CATEGORY - 1) / CATEGORY;
    size_t page = 0;

    while (page < MODEL_PAGES && categories[page] < wanted) {
        page++;
    }
    return page < MODEL_PAGES ? page : SIZE_MAX;
}

static size_t find_page(size_t needed)
{
    size_t heap_page = SIZE_MAX;

    return free_space_map_find(map, needed, &heap_page) == 0 ? heap_page : SIZE_MAX;
}

static void compare_with_model(const uint8_t *categories, uint64_t *random)
{
    size_t needed = next_random(random) % (FREE_SPACE_MAP_LARGEST_REQUEST + CATEGORY);
    size_t expected =
        needed > FREE_SPACE_MAP_LARGEST_REQUEST ? SIZE_MAX : model_find(categories, needed);
    size_t heap_page = SIZE_MAX;
    int status = free_space_map_find(map, needed, &heap_page);

    if (expected == SIZE_MAX) {
        assert_int_equal(status, -1);
        return;
    }
    assert_int_equal(status, 0);
    assert_int_equal(heap_page, expected);
}

static void model_step(uint8_t *categories, uint64_t *random, size_t step)
{
    size_t page = next_random(random) % MODEL_PAGES;
    size_t free_bytes = next_random(random) % (MOST_FREE + 1);

    record(page, free_bytes);
    categories[page] = (uint8_t) (free_bytes / CATEGORY);
    compare_with_model(categories, random);
    if (step % RESTART_EVERY == RESTART_EVERY - 1) {
        restart();
    }
}

static void run_against_model(uint64_t seed)
{
    uint8_t categories[MODEL_PAGES] = {};
    uint64_t random = seed;

    print_message("seed %llu\n", (unsigned long long) seed);
    for (size_t step = 0; step < MODEL_STEPS; step++) {
        model_step(categories, &random, step);
    }
}

static void forget_every_page(void)
{
    for (size_t page = 0; page < MODEL_PAGES; page++) {
        record(page, 0);
    }
}

static void test_the_map_agrees_with_a_model_under_random_operations([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261106};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        run_against_model(seeds[i]);
        forget_every_page();
    }
}

static constexpr size_t THREADS = 4;
static constexpr size_t OPERATIONS_PER_THREAD = 1500;

typedef struct {
    size_t id;
    uint8_t *categories;
    int errors;
} worker_t;

static void *work(void *argument)
{
    worker_t *worker = argument;
    uint64_t random = worker->id + 1;

    for (size_t step = 0; step < OPERATIONS_PER_THREAD; step++) {
        size_t owned = next_random(&random) % (MODEL_PAGES / THREADS);
        size_t page = (owned * THREADS) + worker->id;
        size_t free_bytes = next_random(&random) % (MOST_FREE + 1);
        size_t needed = next_random(&random) % (FREE_SPACE_MAP_LARGEST_REQUEST + 1);
        size_t found = 0;

        worker->errors += free_space_map_record(map, page, free_bytes) == 0 ? 0 : 1;
        worker->categories[page] = (uint8_t) (free_bytes / CATEGORY);
        worker->errors += free_space_map_find(map, needed, &found) == 0 || errno == ENOENT ? 0 : 1;
    }
    return nullptr;
}

// Each thread owns the pages whose number has its remainder, so the last record of a page is known
static void test_threads_do_not_lose_updates([[maybe_unused]] void **state)
{
    uint8_t categories[MODEL_PAGES] = {};
    worker_t workers[THREADS] = {};
    pthread_t threads[THREADS];

    for (size_t i = 0; i < THREADS; i++) {
        workers[i] = (worker_t){.id = i, .categories = categories};
        assert_int_equal(pthread_create(&threads[i], nullptr, work, &workers[i]), 0);
    }
    for (size_t i = 0; i < THREADS; i++) {
        assert_int_equal(pthread_join(threads[i], nullptr), 0);
        assert_int_equal(workers[i].errors, 0);
    }
    for (size_t needed = 0; needed <= FREE_SPACE_MAP_LARGEST_REQUEST; needed += CATEGORY) {
        assert_int_equal(model_find(categories, needed), find_page(needed));
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_a_new_map_knows_no_page, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_record_rounds_down_and_a_search_rounds_up, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_the_search_returns_the_first_page_that_is_large_enough,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_largest_values_are_accepted, setup, teardown),
    cmocka_unit_test_setup_teardown(test_values_out_of_range_are_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_map_survives_a_restart, setup, teardown),
    cmocka_unit_test_setup_teardown(test_nothing_stays_pinned, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_torn_page_zero_gives_an_empty_map, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_file_of_another_page_type_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_file_that_ends_in_a_partial_page_is_refused, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_failing_reads_and_sizes_are_reported, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_failing_read_of_page_zero_fails_the_calls, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_page_zero_that_changed_type_is_reported, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_search_repairs_inner_nodes_that_promise_too_much, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_threads_do_not_lose_updates, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_map_agrees_with_a_model_under_random_operations, setup,
                                    teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
