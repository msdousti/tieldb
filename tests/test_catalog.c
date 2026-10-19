/**
 * Tests for the catalog
 *
 * The catalog is shared by every test in this file, so each test that creates a table uses a name
 * of its own.
 */

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "storage/catalog.h"
#include "table_assert.h"

static const table_column_t PAIR_COLUMNS[] = {{"a", &TYPE_INT4}, {"b", &TYPE_TEXT}};

static void assert_column_is(const table_t *table, size_t index, const char *name,
                             const type_info_t *type)
{
    assert_string_equal(table_column(table, index)->name, name);
    assert_ptr_equal(table_column(table, index)->type, type);
}

static table_t *create_or_fail(const char *name)
{
    table_t *table = catalog_create_table(name, PAIR_COLUMNS, 2);
    if (table == nullptr) {
        fail();
    }
    return table;
}

static void test_unknown_table_is_not_found([[maybe_unused]] void **state)
{
    assert_null(catalog_find("nope"));
}

static void test_created_table_can_be_found([[maybe_unused]] void **state)
{
    table_t *created = create_or_fail("found");

    assert_ptr_equal(catalog_find("found"), created);
    assert_string_equal(table_name(created), "found");
    assert_int_equal(count_rows(created), 0);
    assert_column_is(created, 1, "b", &TYPE_TEXT);
}

static void test_creating_an_existing_table_is_an_error([[maybe_unused]] void **state)
{
    table_t *first = create_or_fail("twice");

    errno = 0;
    assert_null(catalog_create_table("twice", PAIR_COLUMNS, 2));
    assert_int_equal(errno, EEXIST);
    assert_ptr_equal(catalog_find("twice"), first);
}

static void test_many_tables_can_be_created([[maybe_unused]] void **state)
{
    char name[16];

    for (size_t i = 0; i < 40; i++) {
        snprintf(name, sizeof(name), "many%zu", i);
        (void) create_or_fail(name);
    }
    for (size_t i = 0; i < 40; i++) {
        snprintf(name, sizeof(name), "many%zu", i);
        assert_string_equal(table_name(catalog_find(name)), name);
    }
}

static constexpr size_t CREATING_THREADS = 4;

static void *create_race_table(void *argument)
{
    table_t *table = catalog_create_table("race", PAIR_COLUMNS, 2);
    *(int *) argument = table != nullptr;
    return nullptr;
}

static void test_exactly_one_concurrent_create_wins([[maybe_unused]] void **state)
{
    pthread_t threads[CREATING_THREADS];
    int created[CREATING_THREADS] = {};
    int winners = 0;

    for (size_t i = 0; i < CREATING_THREADS; i++) {
        assert_int_equal(pthread_create(&threads[i], nullptr, create_race_table, &created[i]), 0);
    }
    for (size_t i = 0; i < CREATING_THREADS; i++) {
        assert_int_equal(pthread_join(threads[i], nullptr), 0);
        winners += created[i];
    }

    assert_int_equal(winners, 1);
    assert_string_equal(table_name(catalog_find("race")), "race");
}

static constexpr size_t TABLES_PER_THREAD = 50;

typedef struct {
    size_t thread;
    int failures;
} worker_t;

static void *create_and_find_own_tables(void *argument)
{
    worker_t *worker = argument;
    char name[24];

    for (size_t i = 0; i < TABLES_PER_THREAD; i++) {
        snprintf(name, sizeof(name), "busy_%zu_%zu", worker->thread, i);
        table_t *created = catalog_create_table(name, PAIR_COLUMNS, 2);
        worker->failures += created == nullptr || catalog_find(name) != created;
    }
    return nullptr;
}

static void test_creating_and_finding_at_the_same_time_works([[maybe_unused]] void **state)
{
    pthread_t threads[CREATING_THREADS];
    worker_t workers[CREATING_THREADS] = {};
    int failures = 0;

    for (size_t i = 0; i < CREATING_THREADS; i++) {
        workers[i].thread = i;
        assert_int_equal(
            pthread_create(&threads[i], nullptr, create_and_find_own_tables, &workers[i]), 0);
    }
    for (size_t i = 0; i < CREATING_THREADS; i++) {
        assert_int_equal(pthread_join(threads[i], nullptr), 0);
        failures += workers[i].failures;
    }

    assert_int_equal(failures, 0);
    assert_string_equal(table_name(catalog_find("busy_3_49")), "busy_3_49");
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_unknown_table_is_not_found),
    cmocka_unit_test(test_created_table_can_be_found),
    cmocka_unit_test(test_creating_an_existing_table_is_an_error),
    cmocka_unit_test(test_many_tables_can_be_created),
    cmocka_unit_test(test_exactly_one_concurrent_create_wins),
    cmocka_unit_test(test_creating_and_finding_at_the_same_time_works),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
