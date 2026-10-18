/**
 * Recognizers for the few SQL statements TielDB understands before it has a real parser.
 *
 * Matching ignores case, surrounding whitespace and one trailing semicolon. Identifiers are
 * folded to lower case, as Postgres does for unquoted names. Text that does not match a
 * statement exactly is simply not recognized; there are no error codes.
 */

#pragma once

#include <stddef.h>

/** Longest identifier plus its terminator, as in Postgres (63 characters). */
constexpr size_t SQL_IDENTIFIER_CAPACITY = 64;

/** Matches "SELECT version()". */
[[nodiscard]] bool sql_is_version_query(const char *sql);

/**
 * Matches "SELECT * FROM <table>" and copies the lower-cased table name into table. Names that
 * do not fit in capacity bytes (with the terminator) are not recognized.
 */
[[nodiscard]] bool sql_parse_select_all(const char *sql, char *table, size_t capacity);
