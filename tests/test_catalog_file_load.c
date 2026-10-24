/**
 * Tests for loading the catalog file
 *
 * Good files come from catalog_file_save. Bad ones are written here, record by record, with the
 * row codec, because they are intact pages (they pass the checksum) that are not good catalogs.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "storage/catalog_file.h"
#include "storage/one_page_file.h"
#include "storage/row_codec.h"

static constexpr size_t FIELDS = 5;
static constexpr size_t MAX_RECORDED = 8;
static constexpr size_t NAME_CAPACITY = 80;

static const int32_t KIND_VERSION = 0;
static const int32_t KIND_TABLE = 1;
static const int32_t KIND_COLUMN = 2;
static const int32_t KIND_UNKNOWN = 7;
static const int32_t VERSION_NOW = CATALOG_FILE_FORMAT_VERSION;
static const int32_t VERSION_NEXT = CATALOG_FILE_FORMAT_VERSION + 1;
static const int32_t OID_INT4 = TYPE_OID_INT4;
static const int32_t OID_TEXT = TYPE_OID_TEXT;
static const int32_t OID_UNKNOWN = 9999;
static const int32_t FIRST = 0;

static const type_info_t *const RECORD_TYPES[] = {&TYPE_INT4, &TYPE_TEXT, &TYPE_TEXT, &TYPE_INT4,
                                                  &TYPE_INT4};
static const row_layout_t RECORD_LAYOUT = {RECORD_TYPES, FIELDS};
static const table_column_t PAIR[] = {{"id", &TYPE_INT4}, {"name", &TYPE_TEXT}};

static const char PATH_TEMPLATE[] = "/tmp/tieldb_catalog_load_XXXXXX";
static char path[sizeof(PATH_TEMPLATE)];

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

typedef struct {
    char name[NAME_CAPACITY];
    char engine[NAME_CAPACITY];
    size_t column_count;
    char column_names[TABLE_MAX_COLUMNS][NAME_CAPACITY];
    const type_info_t *types[TABLE_MAX_COLUMNS];
    size_t display_position[TABLE_MAX_COLUMNS];
} recorded_table_t;

typedef struct {
    size_t count;
    size_t fail_at; // the visit with this index fails with EIO; MAX_RECORDED for none
    recorded_table_t tables[MAX_RECORDED];
} recorder_t;

static int record_table(const catalog_file_table_t *table, void *context)
{
    recorder_t *recorder = context;
    recorded_table_t *recorded = &recorder->tables[recorder->count];

    snprintf(recorded->name, NAME_CAPACITY, "%s", table->name);
    snprintf(recorded->engine, NAME_CAPACITY, "%s", table->engine);
    recorded->column_count = table->column_count;
    for (size_t i = 0; i < table->column_count; i++) {
        snprintf(recorded->column_names[i], NAME_CAPACITY, "%s", table->columns[i].name);
        recorded->types[i] = table->columns[i].type;
        recorded->display_position[i] = table->display_position[i];
    }
    if (recorder->count++ == recorder->fail_at) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static recorder_t load_all(void)
{
    static recorder_t recorder;

    recorder = (recorder_t){.fail_at = MAX_RECORDED};
    assert_int_equal(catalog_file_load(path, record_table, &recorder), 0);
    return recorder;
}

static void assert_pet_table(const recorded_table_t *table)
{
    assert_string_equal(table->name, "pet");
    assert_string_equal(table->engine, "one_page");
    assert_int_equal(table->column_count, 2);
    assert_string_equal(table->column_names[1], "name");
    assert_ptr_equal(table->types[0], &TYPE_INT4);
    assert_ptr_equal(table->types[1], &TYPE_TEXT);
    assert_int_equal(table->display_position[0], 1);
    assert_int_equal(table->display_position[1], 0);
}

static void test_saved_tables_come_back_with_their_columns([[maybe_unused]] void **state)
{
    table_t *tables[] = {table_create("pet", PAIR, 2), table_create("empty", nullptr, 0)};
    const size_t order[] = {1, 0};
    recorder_t recorder;

    assert_int_equal(table_set_display_order(tables[0], order, 2), 0);
    assert_int_equal(catalog_file_save(path, tables, 2), 0);
    recorder = load_all();

    assert_int_equal(recorder.count, 2);
    assert_pet_table(&recorder.tables[0]);
    assert_string_equal(recorder.tables[1].name, "empty");
    assert_int_equal(recorder.tables[1].column_count, 0);
    table_destroy(tables[0]);
    table_destroy(tables[1]);
}

static void test_a_catalog_without_tables_loads_nothing([[maybe_unused]] void **state)
{
    assert_int_equal(catalog_file_save(path, nullptr, 0), 0);
    assert_int_equal(load_all().count, 0);
}

static void test_a_missing_file_is_reported([[maybe_unused]] void **state)
{
    recorder_t recorder = {.fail_at = MAX_RECORDED};

    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, ENOENT);
}

static void test_the_error_of_the_visitor_stops_the_load([[maybe_unused]] void **state)
{
    table_t *tables[] = {table_create("a", PAIR, 2), table_create("b", PAIR, 2)};
    recorder_t recorder = {.fail_at = 0};

    assert_int_equal(catalog_file_save(path, tables, 2), 0);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EIO);
    assert_int_equal(recorder.count, 1);
    recorder = (recorder_t){.fail_at = 1};
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EIO);
    assert_int_equal(recorder.count, 2);
    table_destroy(tables[0]);
    table_destroy(tables[1]);
}

typedef struct {
    const int32_t *kind;
    const char *table;
    const char *name;
    const int32_t *first;
    const int32_t *second;
} spec_t;

static spec_t version_row(const int32_t *version)
{
    return (spec_t){&KIND_VERSION, nullptr, nullptr, version, nullptr};
}

static spec_t table_row(const char *name)
{
    return (spec_t){&KIND_TABLE, name, "one_page", nullptr, nullptr};
}

static spec_t column_row(const char *table, const char *name, const int32_t *oid)
{
    return (spec_t){&KIND_COLUMN, table, name, oid, &FIRST};
}

static void add_row(page_t *page, const spec_t *spec)
{
    unsigned char row[PAGE_SIZE];
    const row_value_t values[] = {{spec->kind, spec->kind == nullptr ? 0 : sizeof(int32_t)},
                                  {spec->table, spec->table == nullptr ? 0 : strlen(spec->table)},
                                  {spec->name, spec->name == nullptr ? 0 : strlen(spec->name)},
                                  {spec->first, spec->first == nullptr ? 0 : sizeof(int32_t)},
                                  {spec->second, spec->second == nullptr ? 0 : sizeof(int32_t)}};
    ssize_t size = row_encode(&RECORD_LAYOUT, values, row, sizeof(row));

    assert_true(size >= 0);
    assert_true(page_insert(page, row, (size_t) size) >= 0);
}

static void write_page(const spec_t *specs, size_t count)
{
    page_t page;

    page_init(&page, PAGE_TYPE_CATALOG);
    for (size_t i = 0; i < count; i++) {
        add_row(&page, &specs[i]);
    }
    assert_int_equal(one_page_file_save(path, &page), 0);
}

static void assert_refused(const spec_t *specs, size_t count)
{
    recorder_t recorder = {.fail_at = MAX_RECORDED};

    write_page(specs, count);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EBADMSG);
}

static void test_a_good_file_written_by_hand_loads([[maybe_unused]] void **state)
{
    const spec_t specs[] = {version_row(&VERSION_NOW), table_row("pet"),
                            column_row("pet", "id", &OID_INT4),
                            column_row("pet", "name", &OID_TEXT)};

    write_page(specs, 4);

    assert_int_equal(load_all().tables[0].column_count, 2);
}

static void test_the_version_row_must_be_first_and_current([[maybe_unused]] void **state)
{
    const spec_t table_first[] = {table_row("pet")};
    const spec_t next_version[] = {version_row(&VERSION_NEXT)};
    const spec_t no_version_number[] = {version_row(nullptr)};
    const spec_t no_kind[] = {{nullptr, nullptr, nullptr, &VERSION_NOW, nullptr}};
    const spec_t numbered_table[] = {{&KIND_TABLE, nullptr, nullptr, &VERSION_NOW, nullptr}};

    assert_refused(numbered_table, 1);
    assert_refused(table_first, 1);
    assert_refused(next_version, 1);
    assert_refused(no_version_number, 1);
    assert_refused(no_kind, 1);
}

static void test_other_kinds_and_a_second_version_are_refused([[maybe_unused]] void **state)
{
    const spec_t unknown[] = {version_row(&VERSION_NOW),
                              {&KIND_UNKNOWN, nullptr, nullptr, nullptr, nullptr}};
    const spec_t twice[] = {version_row(&VERSION_NOW), version_row(&VERSION_NOW)};
    const spec_t no_kind[] = {version_row(&VERSION_NOW),
                              {nullptr, "pet", nullptr, nullptr, nullptr}};

    assert_refused(unknown, 2);
    assert_refused(twice, 2);
    assert_refused(no_kind, 2);
}

static void test_a_column_must_belong_to_the_table_before_it([[maybe_unused]] void **state)
{
    const spec_t first[] = {version_row(&VERSION_NOW), column_row("pet", "id", &OID_INT4)};
    const spec_t other[] = {version_row(&VERSION_NOW), table_row("pet"),
                            column_row("dog", "id", &OID_INT4)};
    const spec_t longer[] = {version_row(&VERSION_NOW), table_row("pet"),
                             column_row("pets", "id", &OID_INT4)};
    const spec_t unnamed[] = {version_row(&VERSION_NOW), table_row("pet"),
                              column_row(nullptr, "id", &OID_INT4)};

    assert_refused(first, 2);
    assert_refused(other, 3);
    assert_refused(longer, 3);
    assert_refused(unnamed, 3);
}

static void test_a_column_row_must_be_complete([[maybe_unused]] void **state)
{
    const spec_t head[] = {version_row(&VERSION_NOW), table_row("pet")};
    spec_t specs[4];

    memcpy(specs, head, sizeof(head));
    specs[2] = column_row("pet", nullptr, &OID_INT4);
    assert_refused(specs, 3);
    specs[2] = column_row("pet", "id", nullptr);
    assert_refused(specs, 3);
    specs[2] = column_row("pet", "id", &OID_UNKNOWN);
    assert_refused(specs, 3);
    specs[2] = column_row("pet", "id", &OID_INT4);
    specs[2].second = nullptr;
    assert_refused(specs, 3);
}

static void test_a_table_row_must_have_names_that_fit([[maybe_unused]] void **state)
{
    static char long_name[CATALOG_FILE_NAME_CAPACITY + 1];
    spec_t specs[2] = {version_row(&VERSION_NOW), table_row(nullptr)};

    memset(long_name, 'x', sizeof(long_name) - 1);
    assert_refused(specs, 2);
    specs[1] = table_row(long_name);
    assert_refused(specs, 2);
    specs[1] = table_row("pet");
    specs[1].name = nullptr;
    assert_refused(specs, 2);
}

static void test_too_many_columns_are_refused([[maybe_unused]] void **state)
{
    spec_t specs[2 + TABLE_MAX_COLUMNS + 1] = {version_row(&VERSION_NOW), table_row("wide")};

    for (size_t i = 0; i < TABLE_MAX_COLUMNS + 1; i++) {
        specs[2 + i] = column_row("wide", "c", &OID_INT4);
    }
    assert_refused(specs, 2 + TABLE_MAX_COLUMNS + 1);
}

static void test_a_file_that_is_not_a_catalog_page_is_refused([[maybe_unused]] void **state)
{
    page_t page;
    recorder_t recorder = {.fail_at = MAX_RECORDED};

    page_init(&page, PAGE_TYPE_HEAP);
    assert_int_equal(one_page_file_save(path, &page), 0);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EBADMSG);
    page_init(&page, PAGE_TYPE_CATALOG);
    assert_int_equal(one_page_file_save(path, &page), 0);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EBADMSG);
}

static void test_a_slot_that_holds_no_row_is_refused([[maybe_unused]] void **state)
{
    page_t page;
    const spec_t version = version_row(&VERSION_NOW);
    const spec_t table = table_row("pet");
    recorder_t recorder = {.fail_at = MAX_RECORDED};

    page_init(&page, PAGE_TYPE_CATALOG);
    add_row(&page, &version);
    add_row(&page, &table);
    assert_int_equal(page_delete(&page, 1), 0);
    assert_int_equal(one_page_file_save(path, &page), 0);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EBADMSG);
    page_init(&page, PAGE_TYPE_CATALOG);
    assert_true(page_insert(&page, "", 0) >= 0);
    assert_int_equal(one_page_file_save(path, &page), 0);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EBADMSG);
}

static void test_a_saved_name_that_is_too_long_to_load_is_refused([[maybe_unused]] void **state)
{
    static char name[CATALOG_FILE_NAME_CAPACITY + 6];
    table_t *table = nullptr;
    recorder_t recorder = {.fail_at = MAX_RECORDED};

    memset(name, 'n', sizeof(name) - 1);
    table = table_create(name, nullptr, 0);
    assert_int_equal(catalog_file_save(path, &table, 1), 0);
    errno = 0;
    assert_int_equal(catalog_file_load(path, record_table, &recorder), -1);
    assert_int_equal(errno, EBADMSG);
    table_destroy(table);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_saved_tables_come_back_with_their_columns, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_catalog_without_tables_loads_nothing, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_missing_file_is_reported, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_error_of_the_visitor_stops_the_load, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_good_file_written_by_hand_loads, setup, teardown),
    cmocka_unit_test_setup_teardown(test_the_version_row_must_be_first_and_current, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_other_kinds_and_a_second_version_are_refused, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_column_must_belong_to_the_table_before_it, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_column_row_must_be_complete, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_table_row_must_have_names_that_fit, setup, teardown),
    cmocka_unit_test_setup_teardown(test_too_many_columns_are_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_file_that_is_not_a_catalog_page_is_refused, setup,
                                    teardown),
    cmocka_unit_test_setup_teardown(test_a_slot_that_holds_no_row_is_refused, setup, teardown),
    cmocka_unit_test_setup_teardown(test_a_saved_name_that_is_too_long_to_load_is_refused, setup,
                                    teardown),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
