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
 * data_dir_checkpoint saves every table in the catalog, one file at a time, and then the catalog.
 * It is not atomic: a crash in the middle leaves some tables saved a step ahead of the catalog,
 * which is what the WAL (a later phase) is for. A checkpoint does not stop inserts; each table is
 * locked while its rows are saved.
 *
 * Errors return -1 and set errno: those of the files they use (catalog_file.h, page_file.h), and
 * those above.
 */

#pragma once

#include <stddef.h>

constexpr int DATA_DIR_FORMAT_VERSION = 1;
constexpr size_t DATA_DIR_PATH_CAPACITY = 256;

[[nodiscard]] int data_dir_init(const char *dir);

[[nodiscard]] int data_dir_checkpoint(const char *dir);
