/**
 * Tests for the data directory: the version file and the checkpoint
 *
 * The catalog is one per process, so the test that needs an empty one comes first. Tables made by
 * the tests stay in the catalog, so each test names its tables differently.
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmocka.h>

#include "io/io.h"
#include "storage/catalog.h"
#include "storage/catalog_file.h"
#include "storage/data_dir.h"
#include "storage/one_page_engine.h"

static constexpr size_t LONG_NAME = 230;

static const table_column_t PAIR[] = {{"id", &TYPE_INT4}, {"name", &TYPE_TEXT}};
static const type_info_t *const PAIR_TYPES[] = {&TYPE_INT4, &TYPE_TEXT};
static const row_layout_t PAIR_LAYOUT = {PAIR_TYPES, 2};
static const char DIR_TEMPLATE[] = "/tmp/tieldb_data_dir_XXXXXX";
static char dir[sizeof(DIR_TEMPLATE)];

static int setup([[maybe_unused]] void **state)
{
    memcpy(dir, DIR_TEMPLATE, sizeof(dir));
    io_init(true);
    return mkdtemp(dir) == nullptr ? -1 : 0;
}

// Removes the files of a directory, and the directory. The tests run one thread, so readdir is fine
// NOLINTBEGIN(concurrency-mt-unsafe)
static void remove_directory(const char *root)
{
    char path[512];
    DIR *handle = opendir(root);
    for (struct dirent *entry = handle == nullptr ? nullptr : readdir(handle); entry != nullptr;
         entry = readdir(handle)) {
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        unlink(path);
    }
    if (handle != nullptr) {
        closedir(handle);
    }
    rmdir(root);
}
// NOLINTEND(concurrency-mt-unsafe)

static int teardown([[maybe_unused]] void **state)
{
    io_init(true);
    remove_directory(dir);
    return 0;
}

static void path_of(char *path, const char *file)
{
    snprintf(path, DATA_DIR_PATH_CAPACITY, "%s/%s", dir, file);
}

static void assert_fails_with(int status, int error)
{
    assert_int_equal(status, -1);
    assert_int_equal(errno, error);
}

typedef struct {
    size_t tables;
    size_t pet_first_display_position;
} saved_catalog_t;

static int note_table(const catalog_file_table_t *table, void *context)
{
    saved_catalog_t *saved = context;

    saved->tables++;
    if (strcmp(table->name, "pet") == 0) {
        saved->pet_first_display_position = table->display_position[0];
    }
    return 0;
}

static saved_catalog_t read_saved_catalog(void)
{
    char path[DATA_DIR_PATH_CAPACITY];
    saved_catalog_t saved = {};

    path_of(path, "catalog");
    assert_int_equal(catalog_file_load(path, note_table, &saved), 0);
    return saved;
}

static void
test_a_checkpoint_of_an_empty_catalog_saves_an_empty_catalog([[maybe_unused]] void **state)
{
    assert_int_equal(data_dir_init(dir), 0);
    assert_int_equal(data_dir_checkpoint(dir), 0);

    assert_int_equal(read_saved_catalog().tables, 0);
}

// A directory below dir, with a name of the given length, so that the paths in it get long
static void make_long_directory(char *path, size_t name_length)
{
    size_t used = strlen(dir);

    memcpy(path, dir, used);
    path[used] = '/';
    memset(path + used + 1, 'a', name_length);
    path[used + 1 + name_length] = '\0';
    assert_int_equal(mkdir(path, 0700), 0);
}

static void test_paths_that_do_not_fit_are_refused([[maybe_unused]] void **state)
{
    char longest[DATA_DIR_PATH_CAPACITY + 64];

    make_long_directory(longest, LONG_NAME);
    assert_fails_with(data_dir_init(longest), ENAMETOOLONG);
    assert_fails_with(data_dir_checkpoint(longest), ENAMETOOLONG);
    remove_directory(longest);
}

static void test_a_table_file_path_that_does_not_fit_is_refused([[maybe_unused]] void **state)
{
    char name[61];
    char shorter[DATA_DIR_PATH_CAPACITY];

    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    (void) catalog_create_table(name, nullptr, 0);
    make_long_directory(shorter, 190 - strlen(dir));

    assert_fails_with(data_dir_checkpoint(shorter), ENAMETOOLONG);
    remove_directory(shorter);
}

static void insert_pet_rows(table_t *pet)
{
    const char *rows[][2] = {{"1", "Rex"}, {"007", "Tom"}};
    const size_t order[] = {1, 0};

    assert_int_equal(table_set_display_order(pet, order, 2), 0);
    for (size_t i = 0; i < 2; i++) {
        assert_int_equal(table_insert(pet, rows[i], 2), 0);
    }
}

static size_t count_rows_of(storage_engine_t *engine)
{
    storage_cursor_t *cursor = storage_open_cursor(engine);
    row_value_t cells[2];
    size_t count = 0;

    while (storage_next(cursor, cells) == 1) {
        count++;
    }
    storage_close_cursor(cursor);
    return count;
}

static size_t rows_in_saved_file(const char *file)
{
    char path[DATA_DIR_PATH_CAPACITY];
    storage_engine_t *engine = nullptr;
    size_t count = 0;

    path_of(path, file);
    engine = one_page_engine_load(&PAIR_LAYOUT, path);
    if (engine == nullptr) {
        fail();
    }
    count = count_rows_of(engine);
    storage_destroy(engine);
    return count;
}

static void test_a_checkpoint_saves_every_table_and_the_catalog([[maybe_unused]] void **state)
{
    table_t *pet = catalog_create_table("pet", PAIR, 2);
    saved_catalog_t saved;

    (void) catalog_create_table("pet_empty", PAIR, 2);
    insert_pet_rows(pet);
    assert_int_equal(data_dir_init(dir), 0);
    assert_int_equal(data_dir_checkpoint(dir), 0);

    saved = read_saved_catalog();
    assert_true(saved.tables >= 2);
    assert_int_equal(saved.pet_first_display_position, 1);
}

static void test_the_saved_table_files_hold_the_rows([[maybe_unused]] void **state)
{
    assert_int_equal(data_dir_checkpoint(dir), 0);

    assert_int_equal(rows_in_saved_file("pet.tbl"), 2);
    assert_int_equal(rows_in_saved_file("pet_empty.tbl"), 0);
}

static void test_a_failing_table_save_fails_the_checkpoint([[maybe_unused]] void **state)
{
    io_init(false);
    io_inject_fault(IO_FAULT_WRITE_ERROR, 1);

    assert_fails_with(data_dir_checkpoint(dir), EIO);
}

static void test_the_version_file_is_written_and_idempotent([[maybe_unused]] void **state)
{
    char path[DATA_DIR_PATH_CAPACITY];
    char text[64] = {};
    FILE *file = nullptr;

    assert_int_equal(data_dir_init(dir), 0);
    assert_int_equal(data_dir_init(dir), 0);
    path_of(path, "TIELDB_VERSION");
    file = fopen(path, "r");
    if (file == nullptr || fgets(text, sizeof(text), file) == nullptr) {
        fail();
    }
    fclose(file);
    assert_string_equal(text, "tieldb-data-dir 1\n");
}

static void test_another_version_is_refused([[maybe_unused]] void **state)
{
    char path[DATA_DIR_PATH_CAPACITY];
    FILE *file = nullptr;

    path_of(path, "TIELDB_VERSION");
    file = fopen(path, "w");
    if (file == nullptr) {
        fail();
    }
    fputs("tieldb-data-dir 2\n", file);
    fclose(file);
    errno = 0;
    assert_fails_with(data_dir_init(dir), EBADMSG);
}

static void test_a_directory_that_does_not_exist_is_refused([[maybe_unused]] void **state)
{
    errno = 0;
    assert_fails_with(data_dir_init("/tmp/tieldb_no_such_directory"), ENOENT);
}

static void test_the_version_file_reports_its_io_errors([[maybe_unused]] void **state)
{
    char path[DATA_DIR_PATH_CAPACITY];
    const io_fault_type_t faults[] = {IO_FAULT_WRITE_ERROR, IO_FAULT_TORN_WRITE,
                                      IO_FAULT_FSYNC_FAIL};

    path_of(path, "TIELDB_VERSION");
    io_init(false);
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
        io_inject_fault(faults[i], 1);
        assert_int_equal(data_dir_init(dir), -1);
        assert_int_equal(unlink(path), 0);
    }
    io_init(true);
    assert_int_equal(data_dir_init(dir), 0);
    io_init(false);
    io_inject_fault(IO_FAULT_READ_ERROR, 1);
    assert_fails_with(data_dir_init(dir), EIO);
}

#define SETUP_TEST(test) cmocka_unit_test_setup_teardown(test, setup, teardown)

static const struct CMUnitTest tests[] = {
    SETUP_TEST(test_a_checkpoint_of_an_empty_catalog_saves_an_empty_catalog),
    SETUP_TEST(test_paths_that_do_not_fit_are_refused),
    SETUP_TEST(test_a_checkpoint_saves_every_table_and_the_catalog),
    SETUP_TEST(test_a_table_file_path_that_does_not_fit_is_refused),
    SETUP_TEST(test_the_saved_table_files_hold_the_rows),
    SETUP_TEST(test_a_failing_table_save_fails_the_checkpoint),
    SETUP_TEST(test_the_version_file_is_written_and_idempotent),
    SETUP_TEST(test_another_version_is_refused),
    SETUP_TEST(test_a_directory_that_does_not_exist_is_refused),
    SETUP_TEST(test_the_version_file_reports_its_io_errors),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
