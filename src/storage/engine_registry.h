/**
 * The engine registry: the storage engines there are, found by name.
 *
 * The catalog file records the name of the engine of each table, and the data directory finds the
 * engine to load a table with by that name. The registry is read-only, so any thread may use it
 * without locking. New engines are added to the list in engine_registry.c.
 *
 * engine_find returns nullptr with errno ENOENT for a name that is not an engine.
 */

#pragma once

#include "storage/storage_engine.h"

[[nodiscard]] const storage_engine_factory_t *engine_find(const char *name);

/** The engine of a table that does not ask for another: the one-page engine, for now. */
[[nodiscard]] const storage_engine_factory_t *engine_default(void);
