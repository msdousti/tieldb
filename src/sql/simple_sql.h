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

/** Matches text with no statement in it: nothing, or only whitespace and semicolons. */
[[nodiscard]] bool sql_is_empty(const char *sql);

/** Matches "SELECT version()". */
[[nodiscard]] bool sql_is_version_query(const char *sql);

/**
 * Matches "SELECT * FROM <table>" and copies the lower-cased table name into table. Names that
 * do not fit in capacity bytes (with the terminator) are not recognized.
 */
[[nodiscard]] bool sql_parse_select_all(const char *sql, char *table, size_t capacity);

/** Most values an INSERT statement may list; the same as the most columns of a table. */
constexpr size_t SQL_MAX_INSERT_VALUES = 32;

/** One literal of a VALUES list: an integer such as -5, or a quoted string such as 'it''s'. */
typedef struct {
    const char *text; // for a string, without the quotes and with '' turned into '
    bool is_quoted;
} sql_literal_t;

typedef struct {
    char table[SQL_IDENTIFIER_CAPACITY];
    sql_literal_t values[SQL_MAX_INSERT_VALUES];
    size_t count;
} sql_insert_t;

/**
 * Matches "INSERT INTO <table> VALUES (<literal>, ...)". The literal texts point into scratch,
 * which must have room for strlen(sql) + 1 bytes and stay alive while insert is used.
 */
[[nodiscard]] bool sql_parse_insert(const char *sql, char *scratch, sql_insert_t *insert);

/** Most columns a CREATE TABLE statement may define; the same as the most columns of a table. */
constexpr size_t SQL_MAX_CREATE_COLUMNS = 32;

/** One column definition. The type is only the name written in the statement, lower-cased. */
typedef struct {
    char name[SQL_IDENTIFIER_CAPACITY];
    char type[SQL_IDENTIFIER_CAPACITY];
} sql_column_def_t;

typedef struct {
    char table[SQL_IDENTIFIER_CAPACITY];
    sql_column_def_t columns[SQL_MAX_CREATE_COLUMNS];
    size_t count;
} sql_create_table_t;

/**
 * Matches "CREATE TABLE <table> (<column> <type>, ...)"; the column list may be empty, as in
 * Postgres. Whether a type exists is not decided here.
 */
[[nodiscard]] bool sql_parse_create_table(const char *sql, sql_create_table_t *create);
