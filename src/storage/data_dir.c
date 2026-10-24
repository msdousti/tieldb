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
