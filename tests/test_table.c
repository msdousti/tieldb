/**
 * Tests for the in-memory tables
 */

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "storage/table.h"

static const table_column_t PAIR_COLUMNS[] = {{"a", &TYPE_INT4}, {"b", &TYPE_TEXT}};

static table_t *create_pair_table(void)
{
    table_t *table = table_create("pair", PAIR_COLUMNS, 2);
    if (table == nullptr) {
        fail();
    }
    return table;
}

static void test_table_copies_its_name_and_column_names([[maybe_unused]] void **state)
{
    char name[] = "kept";
    char column_name[] = "col";
    const table_column_t columns[] = {{column_name, &TYPE_TEXT}};
    table_t *table = table_create(name, columns, 1);
    if (table == nullptr) {
        fail();
    }

    name[0] = 'X';
    column_name[0] = 'X';

    assert_string_equal(table_name(table), "kept");
    assert_string_equal(table_column(table, 0)->name, "col");
    assert_ptr_equal(table_column(table, 0)->type, &TYPE_TEXT);
    table_destroy(table);
}

static void assert_display_order_rejected(table_t *table, const size_t *order, size_t count)
{
    errno = 0;
    assert_int_equal(table_set_display_order(table, order, count), -1);
    assert_int_equal(errno, EINVAL);
}

static void test_table_without_columns_counts_its_rows([[maybe_unused]] void **state)
{
    table_t *table = table_create("none", nullptr, 0);
    const char *no_cells[1] = {};
    if (table == nullptr) {
        fail();
    }

    assert_int_equal(table_column_count(table), 0);
    assert_int_equal(table_row_count(table), 0);
    assert_int_equal(table_insert(table, no_cells, 0), 0);
    assert_int_equal(table_insert(table, no_cells, 0), 0);
    assert_int_equal(table_row_count(table), 2);
    table_destroy(table);
}

static void test_new_table_is_empty([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();

    assert_int_equal(table_row_count(table), 0);
    table_destroy(table);
}

static void test_inserted_rows_are_copied_and_kept_in_order([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    char text[] = "one";
    const char *first[] = {"1", text};
    const char *second[] = {"2", "two"};

    assert_int_equal(table_insert(table, first, 2), 0);
    text[0] = 'X';
    assert_int_equal(table_insert(table, second, 2), 0);

    assert_int_equal(table_row_count(table), 2);
    assert_string_equal(table_cell(table, 0, 1), "one");
    assert_string_equal(table_cell(table, 1, 0), "2");
    assert_string_equal(table_cell(table, 1, 1), "two");
    table_destroy(table);
}

static void test_table_grows_past_its_initial_capacity([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    const char *row[] = {"7", "seven"};

    for (size_t i = 0; i < 100; i++) {
        assert_int_equal(table_insert(table, row, 2), 0);
    }

    assert_int_equal(table_row_count(table), 100);
    assert_string_equal(table_cell(table, 99, 1), "seven");
    table_destroy(table);
}

static void test_insert_rejects_bad_arguments([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    const char *row[] = {"1", "x"};

    errno = 0;
    assert_int_equal(table_insert(nullptr, row, 2), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(table_insert(table, nullptr, 2), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(table_insert(table, row, 1), -1);
    assert_int_equal(errno, EINVAL);
    errno = 0;
    assert_int_equal(table_insert_locked(nullptr, row, 2), -1);
    assert_int_equal(errno, EINVAL);
    assert_int_equal(table_row_count(table), 0);
    table_destroy(table);
}

static void test_columns_are_shown_in_definition_order_by_default([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();

    assert_int_equal(table_physical_index(table, 0), 0);
    assert_int_equal(table_physical_index(table, 1), 1);
    table_destroy(table);
}

static void
test_display_order_can_be_changed_without_touching_the_rows([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    const char *row[] = {"1", "one"};
    const size_t reversed[] = {1, 0};

    assert_int_equal(table_insert(table, row, 2), 0);
    assert_int_equal(table_set_display_order(table, reversed, 2), 0);

    assert_int_equal(table_physical_index(table, 0), 1);
    assert_int_equal(table_physical_index(table, 1), 0);
    assert_string_equal(table_column(table, 0)->name, "a");
    assert_string_equal(table_cell(table, 0, 0), "1");
    assert_string_equal(table_cell(table, 0, 1), "one");
    table_destroy(table);
}

static void test_invalid_display_orders_are_rejected([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    const size_t duplicate[] = {1, 1};
    const size_t out_of_range[] = {0, 2};
    const size_t too_short[] = {0};
    const size_t reversed[] = {1, 0};

    assert_int_equal(table_set_display_order(table, reversed, 2), 0);
    assert_display_order_rejected(table, duplicate, 2);
    assert_display_order_rejected(table, out_of_range, 2);
    assert_display_order_rejected(table, too_short, 1);
    assert_display_order_rejected(table, nullptr, 2);
    assert_display_order_rejected(nullptr, reversed, 2);
    assert_int_equal(table_physical_index(table, 0), 1);
    table_destroy(table);
}

static constexpr size_t REORDER_ROUNDS = 500;

static void *reorder_columns(void *argument)
{
    const size_t orders[][2] = {{0, 1}, {1, 0}};
    int status = 0;

    for (size_t i = 0; i < REORDER_ROUNDS && status == 0; i++) {
        status = table_set_display_order(argument, orders[i % 2], 2);
    }
    return status == 0 ? nullptr : argument;
}

static void test_readers_see_a_consistent_display_order([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    pthread_t thread;
    void *failed = nullptr;
    size_t inconsistent = 0;

    assert_int_equal(pthread_create(&thread, nullptr, reorder_columns, table), 0);
    for (size_t i = 0; i < REORDER_ROUNDS; i++) {
        table_lock(table);
        inconsistent += table_physical_index(table, 0) == table_physical_index(table, 1);
        table_unlock(table);
    }
    assert_int_equal(pthread_join(thread, &failed), 0);

    assert_null(failed);
    assert_int_equal(inconsistent, 0);
    table_destroy(table);
}

static constexpr size_t INSERT_THREADS = 4;
static constexpr size_t ROWS_PER_THREAD = 200;

static void *insert_rows(void *argument)
{
    const char *row[] = {"1", "x"};
    int status = 0;

    for (size_t i = 0; i < ROWS_PER_THREAD && status == 0; i++) {
        status = table_insert(argument, row, 2);
    }
    return status == 0 ? nullptr : argument;
}

static void test_concurrent_inserts_are_all_kept([[maybe_unused]] void **state)
{
    table_t *table = create_pair_table();
    pthread_t threads[INSERT_THREADS];
    void *failed = nullptr;

    for (size_t i = 0; i < INSERT_THREADS; i++) {
        assert_int_equal(pthread_create(&threads[i], nullptr, insert_rows, table), 0);
    }
    for (size_t i = 0; i < INSERT_THREADS; i++) {
        assert_int_equal(pthread_join(threads[i], &failed), 0);
        assert_null(failed);
    }

    table_lock(table);
    assert_int_equal(table_row_count(table), INSERT_THREADS * ROWS_PER_THREAD);
    table_unlock(table);
    table_destroy(table);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_table_copies_its_name_and_column_names),
    cmocka_unit_test(test_table_without_columns_counts_its_rows),
    cmocka_unit_test(test_new_table_is_empty),
    cmocka_unit_test(test_inserted_rows_are_copied_and_kept_in_order),
    cmocka_unit_test(test_table_grows_past_its_initial_capacity),
    cmocka_unit_test(test_insert_rejects_bad_arguments),
    cmocka_unit_test(test_columns_are_shown_in_definition_order_by_default),
    cmocka_unit_test(test_display_order_can_be_changed_without_touching_the_rows),
    cmocka_unit_test(test_invalid_display_orders_are_rejected),
    cmocka_unit_test(test_readers_see_a_consistent_display_order),
    cmocka_unit_test(test_concurrent_inserts_are_all_kept),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
