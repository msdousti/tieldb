#define _POSIX_C_SOURCE 200809L

/**
 * Tests for the buffer pool
 *
 * Each test has a directory with two page files of four pages: the page n of file f holds the
 * text "f:n".
 */

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "buffer/buffer_pool.h"
#include "ds/clock_policy.h"
#include "test_random.h"
#include "io_fault.h"
#include "test_directory.h"

static constexpr size_t FRAMES = 3;
static constexpr size_t FILES = 2;
static constexpr size_t PAGES_PER_FILE = 4;

static char directory[] = "/tmp/tieldb_buffer_pool_XXXXXX";
static bool use_direct;
static bool use_direct;
static page_file_t *files[FILES];
static buffer_pool_t *pool;
static size_t file_numbers[FILES];

static void file_path(char *path, size_t size, size_t file)
{
    snprintf(path, size, "%s/file%zu", directory, file);
}

static void write_text(page_file_t *file, size_t page_number, const char *text)
{
    page_t page;
    page_init(&page, PAGE_TYPE_HEAP);
    assert_int_equal(page_insert(&page, text, strlen(text)), 0);
    assert_int_equal(page_file_write(file, page_number, &page), 0);
}

static void fill_file(size_t file)
{
    char text[16];

    for (size_t n = 0; n < PAGES_PER_FILE; n++) {
        snprintf(text, sizeof(text), "%zu:%zu", file, n);
        write_text(files[file], n, text);
    }
}

static void create_file(size_t file)
{
    char path[128];

    file_path(path, sizeof(path), file);
    files[file] = use_direct ? page_file_create_direct(path) : page_file_create(path);
    assert_true(files[file] != nullptr);
    fill_file(file);
    assert_int_equal(buffer_pool_add_file(pool, files[file], &file_numbers[file]), 0);
}

static int setup([[maybe_unused]] void **state)
{
    io_init(true);
    make_directory(directory);
    pool = buffer_pool_create(FRAMES, clock_policy_create(FRAMES));
    for (size_t file = 0; file < FILES; file++) {
        create_file(file);
    }
    return 0;
}

static int teardown([[maybe_unused]] void **state)
{
    io_init(true);
    assert_int_equal(buffer_pool_destroy(pool), 0);
    for (size_t file = 0; file < FILES; file++) {
        assert_int_equal(page_file_close(files[file]), 0);
    }
    remove_directory(directory);
    strcpy(directory, "/tmp/tieldb_buffer_pool_XXXXXX");
    return 0;
}

static page_id_t id_of(size_t file, size_t page_number)
{
    return (page_id_t){.file_number = file_numbers[file], .page_number = page_number};
}

static page_t *pin(size_t file, size_t page_number)
{
    page_t *page = nullptr;

    assert_int_equal(buffer_pool_pin(pool, id_of(file, page_number), &page), 0);
    return page;
}

static void unpin(size_t file, size_t page_number)
{
    assert_int_equal(buffer_pool_unpin(pool, id_of(file, page_number)), 0);
}

static void mark_changed(size_t file, size_t page_number)
{
    assert_int_equal(buffer_pool_mark_changed(pool, id_of(file, page_number)), 0);
}

static void assert_text(const page_t *page, const char *text)
{
    size_t length = 0;
    const void *tuple = page_get(page, 0, &length);

    assert_true(tuple != nullptr);
    assert_int_equal(length, strlen(text));
    assert_memory_equal(tuple, text, length);
}

static void assert_pin_fails(size_t file, size_t page_number, int error)
{
    page_t *page = nullptr;

    errno = 0;
    assert_int_equal(buffer_pool_pin(pool, id_of(file, page_number), &page), -1);
    assert_int_equal(errno, error);
}

static void test_a_pinned_page_holds_what_the_file_holds([[maybe_unused]] void **state)
{
    page_t *page = pin(0, 2);

    assert_text(page, "0:2");
    assert_true(pin(0, 2) == page);
    unpin(0, 2);
    unpin(0, 2);
}

static void test_the_same_page_number_in_two_files_is_two_pages([[maybe_unused]] void **state)
{
    page_t *first = pin(0, 1);
    page_t *second = pin(1, 1);

    assert_true(first != second);
    assert_text(second, "1:1");
    unpin(0, 1);
    unpin(1, 1);
}

static void test_unpinning_what_is_not_pinned_is_refused([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(buffer_pool_unpin(pool, id_of(0, 0)), -1);
    assert_int_equal(errno, EINVAL);
    assert_true(pin(0, 0) != nullptr);
    unpin(0, 0);
    errno = 0;
    assert_int_equal(buffer_pool_unpin(pool, id_of(0, 0)), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_a_full_pool_refuses_to_pin_another_page([[maybe_unused]] void **state)
{
    for (size_t n = 0; n < FRAMES; n++) {
        assert_true(pin(0, n) != nullptr);
    }
    assert_pin_fails(0, FRAMES, ENOBUFS);
    for (size_t n = 0; n < FRAMES; n++) {
        unpin(0, n);
    }
}

static void test_a_page_that_cannot_be_read_takes_no_frame([[maybe_unused]] void **state)
{
    for (size_t attempt = 0; attempt < FRAMES + 1; attempt++) {
        assert_pin_fails(0, PAGES_PER_FILE, ENOENT);
    }
    for (size_t n = 0; n < FRAMES; n++) {
        assert_true(pin(0, n) != nullptr);
        unpin(0, n);
    }
}

static void test_a_file_that_was_not_added_is_refused([[maybe_unused]] void **state)
{
    page_t *page = nullptr;

    errno = 0;
    assert_int_equal(buffer_pool_pin(pool, (page_id_t){.file_number = FILES}, &page), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_a_pool_needs_a_frame([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(buffer_pool_create(0, clock_policy_create(1)));
    assert_int_equal(errno, EINVAL);
}

static void test_a_pool_needs_a_policy([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(buffer_pool_create(FRAMES, nullptr));
    assert_int_equal(errno, EINVAL);
}

static void test_destroying_a_pool_with_a_pin_reports_it([[maybe_unused]] void **state)
{
    buffer_pool_t *other = buffer_pool_create(1, clock_policy_create(1));

    assert_true(other != nullptr);
    assert_int_equal(buffer_pool_add_file(other, files[0], &(size_t){}), 0);
    assert_int_equal(buffer_pool_pin(other, (page_id_t){}, &(page_t *){}), 0);
    errno = 0;
    assert_int_equal(buffer_pool_destroy(other), -1);
    assert_int_equal(errno, EBUSY);
}

static void change_page(page_t *page)
{
    assert_int_equal(page_insert(page, "new", 3), 1);
}

static void change_and_unpin(size_t file, size_t page_number)
{
    change_page(pin(file, page_number));
    mark_changed(file, page_number);
    unpin(file, page_number);
}

static void assert_file_page_slot_count(size_t file, size_t page_number, size_t count)
{
    page_t page;

    assert_int_equal(page_file_read(files[file], page_number, &page), 0);
    assert_int_equal(page_slot_count(&page), count);
}

static void assert_flush_fails(int error)
{
    errno = 0;
    assert_int_equal(buffer_pool_flush_all(pool), -1);
    assert_int_equal(errno, error);
}

static void test_flushing_writes_the_changed_pages_only([[maybe_unused]] void **state)
{
    change_and_unpin(0, 1);
    change_page(pin(0, 2));
    unpin(0, 2);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 1, 2);
    assert_file_page_slot_count(0, 2, 1);
}

static void test_a_marked_page_is_written_while_it_is_pinned([[maybe_unused]] void **state)
{
    change_page(pin(0, 0));
    mark_changed(0, 0);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 0, 2);
    unpin(0, 0);
}

static void test_only_a_pinned_page_can_be_marked_changed([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(buffer_pool_mark_changed(pool, id_of(0, 0)), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_a_flushed_page_is_not_written_again([[maybe_unused]] void **state)
{
    change_and_unpin(0, 0);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
}

static void test_a_failed_write_keeps_the_page_changed([[maybe_unused]] void **state)
{
    change_and_unpin(0, 0);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    assert_flush_fails(EIO);
    io_init(true);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 0, 2);
}

static void test_a_failed_sync_is_reported([[maybe_unused]] void **state)
{
    io_init(false);
    io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);
    assert_flush_fails(EIO);
}

// A policy that the test controls: it returns the frame in victim, if that frame is evictable
typedef struct {
    replacement_policy_t base;
    bool is_evictable[FRAMES];
    size_t accesses[FRAMES];
    size_t victim;
    size_t picks;
    size_t destroyed;
} script_policy_t;

static script_policy_t script;

static void script_destroy(replacement_policy_t *policy)
{
    ((script_policy_t *) policy)->destroyed++;
}

static void script_record_access(replacement_policy_t *policy, size_t frame)
{
    ((script_policy_t *) policy)->accesses[frame]++;
}

static void script_set_evictable(replacement_policy_t *policy, size_t frame, bool is_evictable)
{
    ((script_policy_t *) policy)->is_evictable[frame] = is_evictable;
}

static int script_pick_victim(replacement_policy_t *policy, size_t *frame)
{
    script_policy_t *scripted = (script_policy_t *) policy;

    scripted->picks++;
    if (!scripted->is_evictable[scripted->victim]) {
        errno = ENOBUFS;
        return -1;
    }
    scripted->is_evictable[scripted->victim] = false;
    *frame = scripted->victim;
    return 0;
}

static const replacement_policy_ops_t SCRIPT_OPS = {
    .destroy = script_destroy,
    .record_access = script_record_access,
    .set_evictable = script_set_evictable,
    .pick_victim = script_pick_victim,
};

// Replaces the pool by one with the scripted policy; the pages are loaded into the frames 0, 1, 2
// in the order of the pins
static void use_script_policy(size_t victim)
{
    assert_int_equal(buffer_pool_destroy(pool), 0);
    script = (script_policy_t){.base = {.ops = &SCRIPT_OPS}, .victim = victim};
    pool = buffer_pool_create(FRAMES, &script.base);
    for (size_t file = 0; file < FILES; file++) {
        assert_int_equal(buffer_pool_add_file(pool, files[file], &file_numbers[file]), 0);
    }
}

static void pin_and_unpin_the_first_pages(size_t count)
{
    for (size_t n = 0; n < count; n++) {
        assert_true(pin(0, n) != nullptr);
        unpin(0, n);
    }
}

static void test_the_policy_is_told_about_pins_and_unpins([[maybe_unused]] void **state)
{
    use_script_policy(0);
    assert_true(pin(0, 0) == pin(0, 0));
    assert_int_equal(script.accesses[0], 2);
    assert_false(script.is_evictable[0]);
    unpin(0, 0);
    assert_false(script.is_evictable[0]);
    unpin(0, 0);
    assert_true(script.is_evictable[0]);
}

static void test_a_full_pool_reuses_the_frame_that_the_policy_picks([[maybe_unused]] void **state)
{
    use_script_policy(1);
    pin_and_unpin_the_first_pages(FRAMES);
    page_t *second = pin(0, 1);
    unpin(0, 1);
    assert_true(pin(0, FRAMES) == second);
    assert_int_equal(script.picks, 1);
    unpin(0, FRAMES);
}

static void test_a_changed_victim_is_written_back([[maybe_unused]] void **state)
{
    use_script_policy(1);
    pin_and_unpin_the_first_pages(FRAMES);
    change_and_unpin(0, 1);
    assert_true(pin(0, FRAMES) != nullptr);
    unpin(0, FRAMES);
    assert_file_page_slot_count(0, 1, 2);
}

static void test_a_victim_that_was_not_marked_is_not_written([[maybe_unused]] void **state)
{
    use_script_policy(0);
    change_page(pin(0, 0));
    unpin(0, 0);
    pin_and_unpin_the_first_pages(FRAMES);
    assert_true(pin(0, FRAMES) != nullptr);
    unpin(0, FRAMES);
    assert_file_page_slot_count(0, 0, 1);
}

static void test_a_failed_write_back_fails_the_pin_and_keeps_the_page([[maybe_unused]] void **state)
{
    page_t *page = nullptr;

    use_script_policy(0);
    change_and_unpin(0, 0);
    pin_and_unpin_the_first_pages(FRAMES);
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);
    errno = 0;
    assert_int_equal(buffer_pool_pin(pool, id_of(0, FRAMES), &page), -1);
    assert_int_equal(errno, EIO);
    assert_true(script.is_evictable[0]);
    io_init(true);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    assert_file_page_slot_count(0, 0, 2);
}

static void
test_a_victim_whose_replacement_cannot_be_read_leaves_a_free_frame([[maybe_unused]] void **state)
{
    use_script_policy(1);
    pin_and_unpin_the_first_pages(FRAMES);
    assert_pin_fails(0, PAGES_PER_FILE, ENOENT);
    assert_int_equal(script.picks, 1);
    assert_true(pin(0, FRAMES) != nullptr);
    assert_int_equal(script.picks, 1);
    unpin(0, FRAMES);
}

static void
test_a_policy_is_destroyed_when_the_pool_cannot_be_created([[maybe_unused]] void **state)
{
    script = (script_policy_t){.base = {.ops = &SCRIPT_OPS}};
    errno = 0;
    assert_true(buffer_pool_create(0, &script.base) == nullptr);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(script.destroyed, 1);
}

static constexpr size_t MODEL_PAGES = FILES * PAGES_PER_FILE;
static constexpr size_t NOT_HELD = SIZE_MAX;

typedef struct {
    size_t slot_count[MODEL_PAGES];
    size_t held[FRAMES];
    uint64_t random;
} model_t;

static page_id_t model_page_id(size_t page)
{
    return id_of(page / PAGES_PER_FILE, page % PAGES_PER_FILE);
}

static size_t held_pages(const model_t *model, size_t page)
{
    size_t count = 0;

    for (size_t i = 0; i < FRAMES; i++) {
        count += model->held[i] == page ? 1 : 0;
    }
    return count;
}

static size_t distinct_held_pages(const model_t *model)
{
    size_t count = 0;

    for (size_t page = 0; page < MODEL_PAGES; page++) {
        count += held_pages(model, page) > 0 ? 1 : 0;
    }
    return count;
}

static void model_pin(model_t *model, size_t holder, size_t page)
{
    page_t *frame = nullptr;
    bool must_fail = held_pages(model, page) == 0 && distinct_held_pages(model) == FRAMES;
    int status = buffer_pool_pin(pool, model_page_id(page), &frame);

    assert_true((status != 0) == must_fail);
    if (status != 0) {
        return;
    }
    assert_int_equal(page_slot_count(frame), model->slot_count[page]);
    if ((next_random(&model->random) & 1U) != 0) {
        assert_true(page_insert(frame, "x", 1) >= 0);
        model->slot_count[page]++;
        assert_int_equal(buffer_pool_mark_changed(pool, model_page_id(page)), 0);
    }
    model->held[holder] = page;
}

static void model_step(model_t *model)
{
    size_t holder = next_random(&model->random) % FRAMES;
    size_t page = next_random(&model->random) % MODEL_PAGES;

    if (model->held[holder] != NOT_HELD) {
        assert_int_equal(buffer_pool_unpin(pool, model_page_id(model->held[holder])), 0);
        model->held[holder] = NOT_HELD;
    } else {
        model_pin(model, holder, page);
    }
}

static void check_the_files_against_the_model(const model_t *model)
{
    for (size_t page = 0; page < MODEL_PAGES; page++) {
        assert_file_page_slot_count(page / PAGES_PER_FILE, page % PAGES_PER_FILE,
                                    model->slot_count[page]);
    }
}

static model_t new_model(uint64_t seed)
{
    model_t model = {.random = seed};

    for (size_t page = 0; page < MODEL_PAGES; page++) {
        model.slot_count[page] = 1;
    }
    for (size_t i = 0; i < FRAMES; i++) {
        model.held[i] = NOT_HELD;
    }
    return model;
}

static void release_holder(const model_t *model, size_t holder)
{
    if (model->held[holder] == NOT_HELD) {
        return;
    }
    assert_int_equal(buffer_pool_unpin(pool, model_page_id(model->held[holder])), 0);
}

static void release_everything(const model_t *model)
{
    for (size_t i = 0; i < FRAMES; i++) {
        release_holder(model, i);
    }
}

static void run_the_model(uint64_t seed)
{
    model_t model = new_model(seed);

    print_message("seed %llu\n", (unsigned long long) seed);
    for (int step = 0; step < 3000; step++) {
        model_step(&model);
    }
    release_everything(&model);
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    check_the_files_against_the_model(&model);
}

static void test_random_use_agrees_with_a_model_of_the_pages_1([[maybe_unused]] void **state)
{
    run_the_model(1);
}

static void test_random_use_agrees_with_a_model_of_the_pages_2([[maybe_unused]] void **state)
{
    run_the_model(42);
}

static void test_random_use_agrees_with_a_model_of_the_pages_3([[maybe_unused]] void **state)
{
    run_the_model(0xdeadbeef);
}

// Each thread uses the pages p with p % THREADS == its index, so that no two threads change a page
// and only the pool itself is shared: its frames, its page table and its policy
static constexpr size_t THREADS = FRAMES;
static constexpr int ROUNDS = 2000;

typedef struct {
    pthread_t thread;
    size_t index;
    uint64_t random;
    size_t errors;
    size_t slot_count[MODEL_PAGES];
} worker_t;

static void count_error(worker_t *worker, int status)
{
    worker->errors += status != 0 ? 1 : 0;
}

static void use_one_page(worker_t *worker)
{
    size_t own_pages = (MODEL_PAGES - worker->index + THREADS - 1) / THREADS;
    size_t page = worker->index + ((next_random(&worker->random) % own_pages) * THREADS);
    page_t *frame = nullptr;

    count_error(worker, buffer_pool_pin(pool, model_page_id(page), &frame));
    count_error(worker, page_slot_count(frame) != worker->slot_count[page] ? -1 : 0);
    count_error(worker, page_insert(frame, "x", 1) < 0 ? -1 : 0);
    worker->slot_count[page]++;
    count_error(worker, buffer_pool_mark_changed(pool, model_page_id(page)));
    count_error(worker, buffer_pool_unpin(pool, model_page_id(page)));
}

static void *run_worker(void *argument)
{
    worker_t *worker = argument;

    for (int round = 0; round < ROUNDS; round++) {
        use_one_page(worker);
    }
    return nullptr;
}

static void start_worker(worker_t *worker, size_t index)
{
    *worker = (worker_t){.index = index, .random = index + 1};
    for (size_t page = 0; page < MODEL_PAGES; page++) {
        worker->slot_count[page] = 1;
    }
    assert_int_equal(pthread_create(&worker->thread, nullptr, run_worker, worker), 0);
}

static void finish_worker(worker_t *worker)
{
    assert_int_equal(pthread_join(worker->thread, nullptr), 0);
    assert_int_equal(worker->errors, 0);
}

static void test_threads_can_share_the_pool([[maybe_unused]] void **state)
{
    static worker_t workers[THREADS];

    for (size_t i = 0; i < THREADS; i++) {
        start_worker(&workers[i], i);
    }
    for (size_t i = 0; i < THREADS; i++) {
        finish_worker(&workers[i]);
    }
    assert_int_equal(buffer_pool_flush_all(pool), 0);
    for (size_t page = 0; page < MODEL_PAGES; page++) {
        assert_file_page_slot_count(page / PAGES_PER_FILE, page % PAGES_PER_FILE,
                                    workers[page % THREADS].slot_count[page]);
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_a_pinned_page_holds_what_the_file_holds, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_same_page_number_in_two_files_is_two_pages, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_unpinning_what_is_not_pinned_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_full_pool_refuses_to_pin_another_page, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_page_that_cannot_be_read_takes_no_frame, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_file_that_was_not_added_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_pool_needs_a_frame, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_pool_needs_a_policy, setup, teardown),
    cmocka_unit_test_setup_teardown(test_destroying_a_pool_with_a_pin_reports_it, setup, teardown),
    cmocka_unit_test_setup_teardown(test_flushing_writes_the_changed_pages_only, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_marked_page_is_written_while_it_is_pinned, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_only_a_pinned_page_can_be_marked_changed, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_flushed_page_is_not_written_again, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_failed_write_keeps_the_page_changed, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_failed_sync_is_reported, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_policy_is_told_about_pins_and_unpins, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_full_pool_reuses_the_frame_that_the_policy_picks, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_changed_victim_is_written_back, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_victim_that_was_not_marked_is_not_written, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_failed_write_back_fails_the_pin_and_keeps_the_page,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(
        test_a_victim_whose_replacement_cannot_be_read_leaves_a_free_frame, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_policy_is_destroyed_when_the_pool_cannot_be_created,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_random_use_agrees_with_a_model_of_the_pages_1, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_random_use_agrees_with_a_model_of_the_pages_2, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_random_use_agrees_with_a_model_of_the_pages_3, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_threads_can_share_the_pool, setup, teardown),
};

// Every test runs twice: with buffered I/O and with direct I/O
int main(void)
{
    use_direct = false;
    int failed = cmocka_run_group_tests(tests, nullptr, nullptr);
    use_direct = true;
    return failed + cmocka_run_group_tests(tests, nullptr, nullptr);
}
