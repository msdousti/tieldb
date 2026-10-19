/**
 * A file that holds exactly one page, for things that are saved and loaded as a whole: the rows
 * of a table in the one-page engine, and the catalog file. It is the first page of an ordinary
 * file of pages (page_file.h), so the page is checked on load in the same way.
 *
 * single_page_file_save creates the file if it does not exist and overwrites it if it does, and
 * makes the page durable. It is not atomic: a crash in the middle leaves a torn page, which
 * single_page_file_load reports as corrupt. single_page_file_load reads an existing file; a
 * missing or empty file is ENOENT.
 *
 * Errors return -1 and set errno, as page_file.h describes.
 */

#pragma once

#include "storage/page.h"

[[nodiscard]] int single_page_file_save(const char *path, const page_t *page);

[[nodiscard]] int single_page_file_load(const char *path, page_t *page);
