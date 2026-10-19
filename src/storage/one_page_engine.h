/**
 * The one-page engine: a table whose rows all live in one 8 KB slotted page.
 *
 * The first storage engine, simple on purpose: it holds a few hundred small rows, and an insert
 * into a full page fails with ENOSPC. Rows are encoded with the row codec. The engine copies the
 * layout, so the caller's array need not outlive the call. Rows are kept in memory, and saved to
 * a file as a whole with storage_save.
 * See storage_engine.h for the interface and its errors.
 */

#pragma once

#include "storage/row_codec.h"
#include "storage/storage_engine.h"

/** The name that the catalog file records for this engine. */
constexpr char ONE_PAGE_ENGINE_NAME[] = "one_page";

/** Returns nullptr with errno ENOMEM when memory runs out. */
[[nodiscard]] storage_engine_t *one_page_engine_create(const row_layout_t *layout);

/**
 * Creates an engine with the rows that storage_save wrote to the file at path, for the same
 * layout. Errors: ENOENT for a missing or empty file, EBADMSG for a file that is corrupt or is not
 * the file of an engine (see page_file.h), ENOMEM.
 */
[[nodiscard]] storage_engine_t *one_page_engine_load(const row_layout_t *layout, const char *path);
