/**
 * Tests for the one-page storage engine, through the storage engine interface
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "storage/one_page_engine.h"
#include "storage/page.h"
#include "test_random.h"

static const type_info_t *const INT_TEXT[] = {&TYPE_INT4, &TYPE_TEXT};
static const row_layout_t PAIR = {INT_TEXT, 2};
static storage_engine_t *engine;

static int setup([[maybe_unused]] void **state)
{
    engine = one_page_engine_create(&PAIR);
    return engine == nullptr ? -1 : 0;
}

static int teardown([[maybe_unused]] void **state)
{
    storage_destroy(engine);
    return 0;
}

static int insert_pair(const int32_t *number, const char *text, size_t text_length)
{
    const row_value_t values[] = {{number, number == nullptr ? 0 : sizeof(*number)},
                                  {text, text_length}};
    return storage_insert(engine, values);
}

static storage_cursor_t *open_cursor(void)
{
    storage_cursor_t *cursor = storage_open_cursor(engine);
    if (cursor == nullptr) {
        fail();
    }
    return cursor;
}

static size_t count_rows(void)
{
    storage_cursor_t *cursor = open_cursor();
    row_value_t values[2];
    size_t count = 0;

    while (storage_next(cursor, values) == 1) {
        count++;
    }
    storage_close_cursor(cursor);
    return count;
}

static void assert_next_is(storage_cursor_t *cursor, const int32_t *number, const char *text)
{
    row_value_t values[2];

    assert_int_equal(storage_next(cursor, values), 1);
    assert_int_equal(values[0].data == nullptr, number == nullptr);
    if (number != nullptr) {
        assert_memory_equal(values[0].data, number, sizeof(*number));
    }
    assert_int_equal(values[1].data == nullptr, text == nullptr);
    if (text != nullptr) {
        assert_int_equal(values[1].length, strlen(text));
        assert_memory_equal(values[1].data, text, strlen(text));
    }
}

static void test_rows_come_back_in_the_order_they_were_inserted([[maybe_unused]] void **state)
{
    const int32_t one = 1;
    const int32_t two = 2;
    storage_cursor_t *cursor = nullptr;
    row_value_t values[2];

    assert_int_equal(count_rows(), 0);
    assert_int_equal(insert_pair(&one, "Rex", 3), 0);
    assert_int_equal(insert_pair(nullptr, "", 0), 0);
    assert_int_equal(insert_pair(&two, nullptr, 0), 0);
    cursor = open_cursor();
    assert_next_is(cursor, &one, "Rex");
    assert_next_is(cursor, nullptr, "");
    assert_next_is(cursor, &two, nullptr);
    assert_int_equal(storage_next(cursor, values), 0);
    storage_close_cursor(cursor);
}

static void test_a_table_without_columns_still_counts_rows([[maybe_unused]] void **state)
{
    const row_layout_t none = {nullptr, 0};
    storage_engine_t *empty = one_page_engine_create(&none);
    storage_cursor_t *cursor = nullptr;
    row_value_t values[1];
    size_t count = 0;

    assert_int_equal(storage_insert(empty, nullptr), 0);
    assert_int_equal(storage_insert(empty, nullptr), 0);
    assert_int_equal(storage_insert(empty, nullptr), 0);
    cursor = storage_open_cursor(empty);
    while (storage_next(cursor, values) == 1) {
        count++;
    }
    assert_int_equal(count, 3);
    storage_close_cursor(cursor);
    storage_destroy(empty);
}

static void test_a_full_page_refuses_rows_and_keeps_the_others([[maybe_unused]] void **state)
{
    const int32_t number = 7;
    size_t inserted = 0;

    while (insert_pair(&number, "some text", 9) == 0) {
        inserted++;
    }
    assert_int_equal(errno, ENOSPC);
    assert_true(inserted > 100);
    assert_int_equal(count_rows(), inserted);
}

static void test_the_largest_row_fits_and_a_larger_one_does_not([[maybe_unused]] void **state)
{
    static char text[PAGE_SIZE * 2];

    memset(text, 'x', sizeof(text));
    errno = 0;
    assert_int_equal(insert_pair(nullptr, text, 8162), -1);
    assert_int_equal(errno, EFBIG);
    assert_int_equal(insert_pair(nullptr, text, 8161), 0);
    errno = 0;
    assert_int_equal(insert_pair(nullptr, text, sizeof(text)), -1);
    assert_int_equal(errno, EFBIG);
    errno = 0;
    assert_int_equal(insert_pair(nullptr, text, ROW_MAX_VALUE_LENGTH + 1), -1);
    assert_int_equal(errno, EFBIG);
    assert_int_equal(count_rows(), 1);
}

static void test_a_bad_value_is_refused_and_changes_nothing([[maybe_unused]] void **state)
{
    const int32_t good = 5;
    const char bad_number[] = "12";

    assert_int_equal(insert_pair(&good, "ok", 2), 0);
    const row_value_t values[] = {{bad_number, 2}, {"x", 1}};
    errno = 0;
    assert_int_equal(storage_insert(engine, values), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(count_rows(), 1);
}

// A type whose size changes after the row is stored, as if the stored bytes and the layout
// disagreed: the cursor must report the row as malformed and not read past it
static void test_a_row_that_does_not_decode_is_an_error([[maybe_unused]] void **state)
{
    type_info_t shifty = TYPE_INT4;
    const type_info_t *types[] = {&shifty};
    const row_layout_t layout = {types, 1};
    storage_engine_t *shifting = one_page_engine_create(&layout);
    const int32_t number = 9;
    const row_value_t value = {&number, sizeof(number)};
    storage_cursor_t *cursor = nullptr;
    row_value_t decoded[1];

    assert_int_equal(storage_insert(shifting, &value), 0);
    shifty.storage_size = 2 * sizeof(number);
    cursor = storage_open_cursor(shifting);
    errno = 0;
    assert_int_equal(storage_next(cursor, decoded), -1);
    assert_int_equal(errno, EINVAL);
    storage_close_cursor(cursor);
    storage_destroy(shifting);
}

static constexpr size_t MAX_MODEL_ROWS = 2100;
static constexpr size_t MAX_MODEL_TEXT = 24;

typedef struct {
    int32_t number;
    bool has_number;
    char text[MAX_MODEL_TEXT];
    size_t text_length;
} model_row_t;

static int insert_random_row(uint64_t *random, model_row_t *row)
{
    row->has_number = next_random(random) % 4 != 0;
    row->number = (int32_t) next_random(random);
    row->text_length = next_random(random) % MAX_MODEL_TEXT;
    memset(row->text, 'a' + (int) (next_random(random) % 26), row->text_length);
    row->text[row->text_length] = '\0';
    return insert_pair(row->has_number ? &row->number : nullptr, row->text, row->text_length);
}

static void assert_scan_matches(const model_row_t *model, size_t count)
{
    storage_cursor_t *cursor = open_cursor();
    row_value_t values[2];

    for (size_t i = 0; i < count; i++) {
        assert_next_is(cursor, model[i].has_number ? &model[i].number : nullptr, model[i].text);
    }
    assert_int_equal(storage_next(cursor, values), 0);
    storage_close_cursor(cursor);
}

static void fill_and_compare_with_model(uint64_t seed)
{
    static model_row_t model[MAX_MODEL_ROWS];
    uint64_t random = seed;
    size_t count = 0;

    print_message("seed %llu\n", (unsigned long long) seed);
    storage_destroy(engine);
    engine = one_page_engine_create(&PAIR);
    while (count < MAX_MODEL_ROWS && insert_random_row(&random, &model[count]) == 0) {
        count++;
    }
    assert_int_equal(errno, ENOSPC);
    assert_true(count > 100);
    assert_scan_matches(model, count);
}

static void test_random_rows_are_stored_and_scanned_like_a_model([[maybe_unused]] void **state)
{
    const uint64_t seeds[] = {1, 42, 0xdeadbeef, 20261021};

    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        fill_and_compare_with_model(seeds[i]);
    }
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_rows_come_back_in_the_order_they_were_inserted, setup,
                                    teardown),
    cmocka_unit_test(test_a_table_without_columns_still_counts_rows),
    cmocka_unit_test_setup_teardown(test_a_full_page_refuses_rows_and_keeps_the_others, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_the_largest_row_fits_and_a_larger_one_does_not, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_bad_value_is_refused_and_changes_nothing, setup,
                                    teardown),
    cmocka_unit_test(test_a_row_that_does_not_decode_is_an_error),
    cmocka_unit_test_setup_teardown(test_random_rows_are_stored_and_scanned_like_a_model, setup,
                                    teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
