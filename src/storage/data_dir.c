#define _POSIX_C_SOURCE 200809L

#include "storage/data_dir.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "io/io.h"
#include "storage/catalog.h"
#include "storage/catalog_file.h"
#include "storage/one_page_engine.h"

static constexpr char VERSION_FILE[] = "TIELDB_VERSION";
static constexpr char CATALOG_FILE[] = "catalog";
static constexpr char TABLE_SUFFIX[] = ".tbl";
static constexpr char VERSION_TEXT_FORMAT[] = "tieldb-data-dir %d\n";
static constexpr size_t VERSION_TEXT_CAPACITY = 32;
static constexpr mode_t FILE_MODE = 0644;

static int make_path(char *path, const char *dir, const char *name, const char *suffix)
{
    int length = snprintf(path, DATA_DIR_PATH_CAPACITY, "%s/%s%s", dir, name, suffix);
    if ((size_t) length >= DATA_DIR_PATH_CAPACITY) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int read_version_text(const char *path, char *text)
{
    int fd = io->open(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    ssize_t count = io->read(fd, text, VERSION_TEXT_CAPACITY - 1);
    int closed = io->close(fd);
    if (count < 0) {
        return -1;
    }
    text[count] = '\0';
    return closed;
}

static int write_version_text(int fd, const char *text)
{
    size_t length = strlen(text);
    ssize_t written = io->write(fd, text, length);
    if (written < 0) {
        return -1;
    }
    if ((size_t) written != length) {
        errno = EIO;
        return -1;
    }
    return io->fsync(fd);
}

static int create_version_file(const char *path, const char *text)
{
    int fd = io->open(path, O_WRONLY | O_CREAT | O_EXCL, FILE_MODE);
    if (fd < 0) {
        return -1;
    }
    int status = write_version_text(fd, text);
    int closed = io->close(fd);
    return status != 0 ? status : closed;
}

static int check_version_file(const char *path, const char *expected)
{
    char actual[VERSION_TEXT_CAPACITY];
    if (read_version_text(path, actual) != 0) {
        return errno == ENOENT ? create_version_file(path, expected) : -1;
    }
    if (strcmp(actual, expected) != 0) {
        errno = EBADMSG;
        return -1;
    }
    return 0;
}

int data_dir_init(const char *dir)
{
    char path[DATA_DIR_PATH_CAPACITY];
    char expected[VERSION_TEXT_CAPACITY];

    (void) snprintf(expected, sizeof(expected), VERSION_TEXT_FORMAT, DATA_DIR_FORMAT_VERSION);
    if (make_path(path, dir, VERSION_FILE, "") != 0) {
        return -1;
    }
    return check_version_file(path, expected);
}

static int place_in_order(size_t *order, const catalog_file_table_t *entry, size_t column)
{
    if (entry->display_position[column] >= entry->column_count) {
        errno = EBADMSG;
        return -1;
    }
    order[entry->display_position[column]] = column;
    return 0;
}

static int restore_display_order(table_t *table, const catalog_file_table_t *entry)
{
    size_t order[TABLE_MAX_COLUMNS] = {};
    int status = 0;
    for (size_t column = 0; status == 0 && column < entry->column_count; column++) {
        status = place_in_order(order, entry, column);
    }
    return status == 0 ? table_set_display_order(table, order, entry->column_count) : status;
}

static int restore_rows(table_t *table, const char *dir)
{
    char path[DATA_DIR_PATH_CAPACITY];
    if (make_path(path, dir, table_name(table), TABLE_SUFFIX) != 0) {
        return -1;
    }
    int status = table_load_rows(table, path);
    if (status != 0 && errno == ENOENT) {
        errno = EBADMSG;
    }
    return status;
}

static int restore_table(const catalog_file_table_t *entry, void *context)
{
    if (strcmp(entry->engine, ONE_PAGE_ENGINE_NAME) != 0) {
        errno = EBADMSG;
        return -1;
    }
    table_t *table = catalog_create_table(entry->name, entry->columns, entry->column_count);
    if (table == nullptr || restore_display_order(table, entry) != 0) {
        return -1;
    }
    return restore_rows(table, context);
}

int data_dir_load(const char *dir)
{
    char path[DATA_DIR_PATH_CAPACITY];
    if (make_path(path, dir, CATALOG_FILE, "") != 0) {
        return -1;
    }
    int status = catalog_file_load(path, restore_table, (void *) dir);
    return status != 0 && errno == ENOENT ? 0 : status;
}

static int save_tables(const char *dir, table_t *const *tables, size_t count)
{
    char path[DATA_DIR_PATH_CAPACITY];
    int status = 0;
    for (size_t i = 0; status == 0 && i < count; i++) {
        status = make_path(path, dir, table_name(tables[i]), TABLE_SUFFIX);
        status = status == 0 ? table_save_rows(tables[i], path) : status;
    }
    return status;
}

int data_dir_checkpoint(const char *dir)
{
    char path[DATA_DIR_PATH_CAPACITY];
    size_t count = 0;
    table_t **tables = catalog_list_tables(&count);
    if (tables == nullptr) {
        return -1;
    }
    int status = save_tables(dir, tables, count);
    status = status == 0 ? make_path(path, dir, CATALOG_FILE, "") : status;
    status = status == 0 ? catalog_file_save(path, tables, count) : status;
    free((void *) tables);
    return status;
}
