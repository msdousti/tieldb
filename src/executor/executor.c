/**
 * Execution of parsed SQL statements against the tables.
 */

#include "executor/executor.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage/table.h"

static constexpr int DECIMAL_BASE = 10;
static constexpr char MISSING_TABLE_SQLSTATE[] = "42P01";
static constexpr char MISSING_TABLE_FORMAT[] = "relation \"%s\" does not exist";
static constexpr char SYNTAX_SQLSTATE[] = "42601";
static constexpr char WRONG_COUNT_MESSAGE[] = "INSERT must supply a value for every column";
static constexpr char INVALID_INTEGER_SQLSTATE[] = "22P02";
static constexpr char INVALID_INTEGER_FORMAT[] = "invalid input syntax for type integer: \"%s\"";

static void set_error(executor_error_t *error, const char *sqlstate)
{
    snprintf(error->sqlstate, sizeof(error->sqlstate), "%s", sqlstate);
}

static bool is_valid_integer(const char *text)
{
    char *end = nullptr;
    errno = 0;
    long value = strtol(text, &end, DECIMAL_BASE);
    return end != text && *end == '\0' && errno == 0 && value >= INT32_MIN && value <= INT32_MAX;
}

static bool is_valid_cell(column_type_t type, const char *text)
{
    return type != COLUMN_TYPE_INTEGER || is_valid_integer(text);
}

static const char *find_invalid_integer(const table_t *table, const sql_insert_t *insert)
{
    const char *invalid = nullptr;
    for (size_t i = 0; i < insert->count && invalid == nullptr; i++) {
        const char *text = insert->values[i].text;
        invalid = is_valid_cell(table_column(table, i)->type, text) ? nullptr : text;
    }
    return invalid;
}

static int insert_row(table_t *table, const sql_insert_t *insert)
{
    const char *cells[SQL_MAX_INSERT_VALUES];
    for (size_t i = 0; i < insert->count; i++) {
        cells[i] = insert->values[i].text;
    }
    return table_insert(table, cells, insert->count);
}

int executor_insert(const sql_insert_t *insert, executor_error_t *error)
{
    *error = (executor_error_t){};
    table_t *table = table_find(insert->table);
    if (table == nullptr) {
        set_error(error, MISSING_TABLE_SQLSTATE);
        snprintf(error->message, sizeof(error->message), MISSING_TABLE_FORMAT, insert->table);
        return 0;
    }
    if (insert->count != table_column_count(table)) {
        set_error(error, SYNTAX_SQLSTATE);
        snprintf(error->message, sizeof(error->message), "%s", WRONG_COUNT_MESSAGE);
        return 0;
    }
    const char *invalid = find_invalid_integer(table, insert);
    if (invalid != nullptr) {
        set_error(error, INVALID_INTEGER_SQLSTATE);
        snprintf(error->message, sizeof(error->message), INVALID_INTEGER_FORMAT, invalid);
        return 0;
    }
    return insert_row(table, insert);
}
