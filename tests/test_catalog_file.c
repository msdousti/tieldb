/**
 * Tests for saving the catalog file
 *
 * The saved page is read back with the row codec, following the format that catalog_file.h
 * describes, so these tests also pin the format down.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "storage/catalog_file.h"
#include "storage/one_page_file.h"
#include "storage/row_codec.h"

static constexpr int32_t KIND_VERSION = 0;
static constexpr int32_t KIND_TABLE = 1;
static constexpr int32_t KIND_COLUMN = 2;
static constexpr size_t FIELDS = 5;
static constexpr int32_t OID_INT4 = TYPE_OID_INT4;
static constexpr int32_t OID_TEXT = TYPE_OID_TEXT;

static const type_info_t *const RECORD_TYPES[] = {&TYPE_INT4, &TYPE_TEXT, &TYPE_TEXT, &TYPE_INT4,
                                                  &TYPE_INT4};
static const row_layout_t RECORD_LAYOUT = {RECORD_TYPES, FIELDS};
static const table_column_t PAIR[] = {{"id", &TYPE_INT4}, {"name", &TYPE_TEXT}};

static const char PATH_TEMPLATE[] = "/tmp/tieldb_catalog_file_XXXXXX";
static char path[sizeof(PATH_TEMPLATE)];
static page_t page;

static int setup([[maybe_unused]] void **state)
{
    memcpy(path, PATH_TEMPLATE, sizeof(path));
    int fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return unlink(path);
}

static int teardown([[maybe_unused]] void **state)
{
    unlink(path);
    return 0;
}

static table_t *make_table(const char *name, const table_column_t *columns, size_t count)
{
    table_t *table = table_create(name, columns, count);
    if (table == nullptr) {
        fail();
    }
    return table;
}

static void load_saved_page(void)
{
    assert_int_equal(one_page_file_load(path, &page), 0);
    assert_int_equal(page_type(&page), PAGE_TYPE_CATALOG);
}

static void read_record(size_t slot, row_value_t *fields)
{
    size_t length = 0;
    const void *row = page_get(&page, slot, &length);
    if (row == nullptr) {
        fail();
    }
    assert_int_equal(row_decode(&RECORD_LAYOUT, row, length, fields), 0);
}

static void assert_text(row_value_t field, const char *expected)
{
    assert_int_equal(field.data == nullptr, expected == nullptr);
    if (expected != nullptr) {
        assert_int_equal(field.length, strlen(expected));
        assert_memory_equal(field.data, expected, field.length);
    }
}

static void assert_optional_number(row_value_t field, const int32_t *expected)
{
    int32_t number = 0;

    if (expected == nullptr) {
        assert_null(field.data);
        return;
    }
    if (field.data == nullptr) {
        fail();
    }
    memcpy(&number, field.data, sizeof(number));
    assert_int_equal(number, *expected);
}

typedef struct {
    int32_t kind;
    const char *table;
    const char *name;
    const int32_t *first_number;
    const int32_t *second_number;
} expected_record_t;

static void assert_record(size_t slot, const expected_record_t *expected)
{
    row_value_t fields[FIELDS];

    read_record(slot, fields);
    assert_optional_number(fields[0], &expected->kind);
    assert_text(fields[1], expected->table);
    assert_text(fields[2], expected->name);
    assert_optional_number(fields[3], expected->first_number);
    assert_optional_number(fields[4], expected->second_number);
}

static void assert_version_record(void)
{
    const int32_t version = CATALOG_FILE_FORMAT_VERSION;

    assert_record(0, &(expected_record_t){KIND_VERSION, nullptr, nullptr, &version, nullptr});
}

static void assert_table_record(size_t slot, const char *name)
{
    assert_record(slot, &(expected_record_t){KIND_TABLE, name, "one_page", nullptr, nullptr});
}

// numbers: the type OID, then the display position
static void assert_column_record(size_t slot, const char *table, const char *name,
                                 const int32_t *numbers)
{
    const expected_record_t expected = {KIND_COLUMN, table, name, &numbers[0], &numbers[1]};

    assert_record(slot, &expected);
}

static void test_an_empty_catalog_has_only_the_version([[maybe_unused]] void **state)
{
    assert_int_equal(catalog_file_save(path, nullptr, 0), 0);

    load_saved_page();
    assert_int_equal(page_slot_count(&page), 1);
    assert_version_record();
}

static void test_a_table_is_a_table_row_and_a_row_for_each_column([[maybe_unused]] void **state)
{
    table_t *table = make_table("pet", PAIR, 2);

    assert_int_equal(catalog_file_save(path, &table, 1), 0);

    load_saved_page();
    assert_int_equal(page_slot_count(&page), 4);
    assert_version_record();
    assert_table_record(1, "pet");
    assert_column_record(2, "pet", "id", (int32_t[]){OID_INT4, 0});
    assert_column_record(3, "pet", "name", (int32_t[]){OID_TEXT, 1});
    table_destroy(table);
}

static void test_the_display_position_of_each_column_is_saved([[maybe_unused]] void **state)
{
    table_t *table = make_table("pet", PAIR, 2);
    const size_t order[] = {1, 0};

    assert_int_equal(table_set_display_order(table, order, 2), 0);
    assert_int_equal(catalog_file_save(path, &table, 1), 0);

    load_saved_page();
    assert_column_record(2, "pet", "id", (int32_t[]){OID_INT4, 1});
    assert_column_record(3, "pet", "name", (int32_t[]){OID_TEXT, 0});
    table_destroy(table);
}

static void test_tables_are_saved_in_the_given_order([[maybe_unused]] void **state)
{
    table_t *tables[] = {make_table("b", PAIR, 1), make_table("empty", nullptr, 0),
                         make_table("a", PAIR, 2)};

    assert_int_equal(catalog_file_save(path, tables, 3), 0);

    load_saved_page();
    assert_int_equal(page_slot_count(&page), 1 + 2 + 1 + 3);
    assert_table_record(1, "b");
    assert_table_record(3, "empty");
    assert_table_record(4, "a");
    for (size_t i = 0; i < 3; i++) {
        table_destroy(tables[i]);
    }
}

static void test_catalog_that_does_not_fit_one_page_is_refused([[maybe_unused]] void **state)
{
    static char names[TABLE_MAX_COLUMNS][61];
    table_column_t columns[TABLE_MAX_COLUMNS];
    table_t *tables[6];

    for (size_t i = 0; i < TABLE_MAX_COLUMNS; i++) {
        memset(names[i], 'a' + (int) (i % 26), sizeof(names[i]) - 1);
        columns[i] = (table_column_t){names[i], &TYPE_INT4};
    }
    for (size_t i = 0; i < 6; i++) {
        tables[i] = make_table("wide", columns, TABLE_MAX_COLUMNS);
    }

    errno = 0;
    assert_int_equal(catalog_file_save(path, tables, 6), -1);
    assert_int_equal(errno, ENOSPC);
    assert_int_equal(access(path, F_OK), -1);
    for (size_t i = 0; i < 6; i++) {
        table_destroy(tables[i]);
    }
}

static void test_a_name_too_long_for_a_row_is_refused([[maybe_unused]] void **state)
{
    static char name[40000];
    table_t *table = nullptr;

    memset(name, 'n', sizeof(name) - 1);
    table = make_table(name, nullptr, 0);
    errno = 0;
    assert_int_equal(catalog_file_save(path, &table, 1), -1);
    assert_int_equal(errno, EFBIG);
    table_destroy(table);
}

static void test_a_path_that_cannot_be_created_is_reported([[maybe_unused]] void **state)
{
    errno = 0;
    assert_int_equal(catalog_file_save("/tmp/tieldb_no_such_directory/catalog", nullptr, 0), -1);
    assert_int_equal(errno, ENOENT);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_an_empty_catalog_has_only_the_version, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_table_is_a_table_row_and_a_row_for_each_column, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_the_display_position_of_each_column_is_saved, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_tables_are_saved_in_the_given_order, setup, teardown),
    cmocka_unit_test_setup_teardown(test_catalog_that_does_not_fit_one_page_is_refused, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_name_too_long_for_a_row_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_path_that_cannot_be_created_is_reported, setup,
                                    teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
