/**
 * The data directory: where a TielDB server keeps its tables between runs.
 *
 *   TIELDB_VERSION   a text file with the format version of the directory
 *   catalog          the catalog file (catalog_file.h): the tables, their columns and types
 *   <table>.tbl      the rows of each table, saved by the table's storage engine
 *
 * The directory must exist; the server does not create it, as initdb does for Postgres. It should
 * be empty or be a TielDB data directory: data_dir_init writes the version file into an empty one
 * and refuses a directory whose version file says another format version (EBADMSG), so that an
 * old or foreign directory is never misread. The path is at most DATA_DIR_PATH_CAPACITY - 1 bytes
 * including a file name (ENAMETOOLONG otherwise).
 *
 * data_dir_load puts the tables of the directory into the catalog (catalog.h), which should be
 * empty (EEXIST for a table that exists), with their rows and display order. A directory without
 * a catalog file is a new database and loads nothing. A catalog that lists a table whose file is
 * missing, a table of an engine that is not in the engine registry (engine_registry.h), or a
 * display position outside the table is EBADMSG.
 *
 * data_dir_lock takes the lock on the directory, so that two servers never use one directory (they
 * would overwrite each other's files). It is an advisory lock on the file tieldb.lock, held until
 * data_dir_unlock or until the process ends, however it ends, so a crash leaves nothing to clean
 * up. A directory that another server holds is EBUSY. The lock goes before data_dir_init.
 *
 * data_dir_checkpoint saves every table in the catalog, one file at a time, and then the catalog,
 * and syncs the directory itself so that the files it created are not lost with their names. It is
 * not atomic: a crash in the middle leaves some tables saved a step ahead of the catalog,
 * which is what the WAL (a later phase) is for. A checkpoint does not stop inserts; each table is
 * locked while its rows are saved.
 *
 * Errors return -1 and set errno: those of the files they use (catalog_file.h, page_file.h), and
 * those above. data_dir_init also syncs the directory, as it may have created the version file.
 */

#pragma once

#include <stddef.h>

constexpr int DATA_DIR_FORMAT_VERSION = 1;
constexpr size_t DATA_DIR_PATH_CAPACITY = 256;

typedef struct {
    int fd;
} data_dir_lock_t;

[[nodiscard]] int data_dir_lock(const char *dir, data_dir_lock_t *lock);

void data_dir_unlock(data_dir_lock_t *lock);

[[nodiscard]] int data_dir_init(const char *dir);

[[nodiscard]] int data_dir_load(const char *dir);

[[nodiscard]] int data_dir_checkpoint(const char *dir);
