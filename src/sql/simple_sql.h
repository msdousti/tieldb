/**
 * Recognizers for the few SQL statements TielDB understands before it has a real parser.
 *
 * Matching ignores case, surrounding whitespace and one trailing semicolon. Text that does not
 * match a statement exactly is simply not recognized; there are no error codes.
 */

#pragma once

#include <stddef.h>

/** Matches "SELECT version()". */
[[nodiscard]] bool sql_is_version_query(const char *sql);
