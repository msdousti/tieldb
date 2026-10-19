/**
 * The one-page engine: a table whose rows all live in one 8 KB slotted page.
 *
 * The first storage engine, simple on purpose: it holds a few hundred small rows, and an insert
 * into a full page fails with ENOSPC. Rows are encoded with the row codec. The engine copies the
 * layout, so the caller's array need not outlive the call. Rows are kept in memory only, for now.
 * See storage_engine.h for the interface and its errors.
 */

#pragma once

#include "storage/row_codec.h"
#include "storage/storage_engine.h"

/** Returns nullptr with errno ENOMEM when memory runs out. */
[[nodiscard]] storage_engine_t *one_page_engine_create(const row_layout_t *layout);
