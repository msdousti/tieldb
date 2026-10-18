/**
 * Execution of parsed SQL statements against the tables.
 */

#include "executor/executor.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage/catalog.h"
#include "types/type_registry.h"

static_assert(SQL_MAX_CREATE_COLUMNS <= TABLE_MAX_COLUMNS);

static constexpr char MISSING_TABLE_SQLSTATE[] = "42P01";
static constexpr char MISSING_TABLE_FORMAT[] = "relation \"%s\" does not exist";
static constexpr char SYNTAX_SQLSTATE[] = "42601";
static constexpr char WRONG_COUNT_MESSAGE[] = "INSERT must supply a value for every column";
static constexpr char INVALID_INTEGER_SQLSTATE[] = "22P02";
static constexpr char UNKNOWN_TYPE_SQLSTATE[] = "42704";
static constexpr char UNKNOWN_TYPE_FORMAT[] = "type \"%s\" does not exist";
static constexpr char DUPLICATE_COLUMN_SQLSTATE[] = "42701";
static constexpr char DUPLICATE_COLUMN_FORMAT[] = "column \"%s\" specified more than once";
static constexpr char TABLE_EXISTS_SQLSTATE[] = "42P07";
static constexpr char TABLE_EXISTS_FORMAT[] = "relation \"%s\" already exists";
static constexpr char INVALID_INTEGER_FORMAT[] = "invalid input syntax for type integer: \"%s\"";

static void set_error(executor_error_t *error, const char *sqlstate)
{
    snprintf(error->sqlstate, sizeof(error->sqlstate), "%s", sqlstate);
}

static const char *find_invalid_integer(const table_t *table, const sql_insert_t *insert)
{
    const char *invalid = nullptr;
    for (size_t i = 0; i < insert->count && invalid == nullptr; i++) {
        const char *text = insert->values[i].text;
        const type_info_t *type = table_column(table, table_physical_index(table, i))->type;
        invalid = type->accepts(text) ? nullptr : text;
    }
    return invalid;
}

static int insert_row(table_t *table, const sql_insert_t *insert)
{
    const char *cells[SQL_MAX_INSERT_VALUES];
    for (size_t i = 0; i < insert->count; i++) {
        cells[table_physical_index(table, i)] = insert->values[i].text;
    }
    return table_insert_locked(table, cells, insert->count);
}

static void set_missing_table_error(executor_error_t *error, const char *table_name)
{
    set_error(error, MISSING_TABLE_SQLSTATE);
    snprintf(error->message, sizeof(error->message), MISSING_TABLE_FORMAT, table_name);
}

static int insert_checked(table_t *table, const sql_insert_t *insert, executor_error_t *error)
{
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

int executor_insert(const sql_insert_t *insert, executor_error_t *error)
{
    *error = (executor_error_t){};
    table_t *table = catalog_find(insert->table);
    if (table == nullptr) {
        set_missing_table_error(error, insert->table);
        return 0;
    }
    table_lock(table);
    int status = insert_checked(table, insert, error);
    table_unlock(table);
    return status;
}

// Fills columns and returns the first unknown type name, or nullptr if all are known
static const char *convert_columns(const sql_create_table_t *create, table_column_t *columns)
{
    const char *unknown = nullptr;
    for (size_t i = 0; i < create->count && unknown == nullptr; i++) {
        columns[i].name = create->columns[i].name;
        columns[i].type = type_find_by_name(create->columns[i].type);
        unknown = columns[i].type == nullptr ? create->columns[i].type : nullptr;
    }
    return unknown;
}

static bool is_named_before(const sql_create_table_t *create, size_t index)
{
    bool found = false;
    for (size_t i = 0; i < index && !found; i++) {
        found = strcmp(create->columns[i].name, create->columns[index].name) == 0;
    }
    return found;
}

static const char *find_duplicate_column(const sql_create_table_t *create)
{
    const char *duplicate = nullptr;
    for (size_t i = 1; i < create->count && duplicate == nullptr; i++) {
        duplicate = is_named_before(create, i) ? create->columns[i].name : nullptr;
    }
    return duplicate;
}

static bool check_create_table(const sql_create_table_t *create, table_column_t *columns,
                               executor_error_t *error)
{
    const char *unknown_type = convert_columns(create, columns);
    if (unknown_type != nullptr) {
        set_error(error, UNKNOWN_TYPE_SQLSTATE);
        snprintf(error->message, sizeof(error->message), UNKNOWN_TYPE_FORMAT, unknown_type);
        return false;
    }
    const char *duplicate = find_duplicate_column(create);
    if (duplicate != nullptr) {
        set_error(error, DUPLICATE_COLUMN_SQLSTATE);
        snprintf(error->message, sizeof(error->message), DUPLICATE_COLUMN_FORMAT, duplicate);
        return false;
    }
    return true;
}

int executor_create_table(const sql_create_table_t *create, executor_error_t *error)
{
    table_column_t columns[TABLE_MAX_COLUMNS];
    *error = (executor_error_t){};
    if (!check_create_table(create, columns, error)) {
        return 0;
    }
    if (catalog_create_table(create->table, columns, create->count) != nullptr) {
        return 0;
    }
    if (errno != EEXIST) {
        return -1;
    }
    set_error(error, TABLE_EXISTS_SQLSTATE);
    snprintf(error->message, sizeof(error->message), TABLE_EXISTS_FORMAT, create->table);
    return 0;
}

void executor_select_all(const char *table_name, executor_select_t *select)
{
    *select = (executor_select_t){};
    select->table = catalog_find(table_name);
    if (select->table == nullptr) {
        set_missing_table_error(&select->error, table_name);
        return;
    }
    table_lock(select->table);
}

void executor_select_release(executor_select_t *select)
{
    if (select->table != nullptr) {
        table_unlock(select->table);
        select->table = nullptr;
    }
}
