/**
 * Handler for the simple Query message ('Q'), hardcoded for now.
 */

#include "net/query_handler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor/executor.h"
#include "sql/simple_sql.h"
#include "storage/table.h"

static constexpr char SERVER_VERSION[] = "TielDB 0.1.0";
static constexpr char VERSION_COLUMN[] = "version";
static constexpr char CURRENT_USER_COLUMN[] = "current_user";
static constexpr char CURRENT_DATABASE_COLUMN[] = "current_database";
static constexpr char SELECT_ONE_TAG[] = "SELECT 1";
static constexpr char ERROR_SEVERITY[] = "ERROR";
static constexpr char UNSUPPORTED_SQLSTATE[] = "0A000";
static constexpr char UNSUPPORTED_MESSAGE[] =
    "only SELECT version(), SELECT current_user, SELECT current_database(), SELECT * FROM "
    "<table>, INSERT INTO <table> VALUES (...) and CREATE TABLE are supported";
static constexpr char INSERT_TAG[] = "INSERT 0 1";
static constexpr char CREATE_TABLE_TAG[] = "CREATE TABLE";
static constexpr char SELECT_TAG_FORMAT[] = "SELECT %zu";
static constexpr size_t TAG_CAPACITY = 32;

// Each step appends one message; context is the table or table name the response is about
typedef int (*response_step_t)(wire_writer_t *out, const void *context);

// The context of the single-value responses
typedef struct {
    const char *column;
    const char *value;
} single_value_t;

static int put_single_description(wire_writer_t *out, const void *context)
{
    const single_value_t *single = context;
    const protocol_column_t column = {single->column, PROTOCOL_TYPE_TEXT};
    return protocol_put_row_description(out, &column, 1);
}

static int put_single_row(wire_writer_t *out, const void *context)
{
    const single_value_t *single = context;
    const protocol_value_t value = {single->value, strlen(single->value)};
    return protocol_put_data_row(out, &value, 1);
}

static int put_select_one_tag(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_command_complete(out, SELECT_ONE_TAG);
}

static int put_unsupported_error(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_error_response(out, ERROR_SEVERITY, UNSUPPORTED_SQLSTATE,
                                       UNSUPPORTED_MESSAGE);
}

static int32_t type_oid(column_type_t type)
{
    return type == COLUMN_TYPE_INTEGER ? PROTOCOL_TYPE_INT4 : PROTOCOL_TYPE_TEXT;
}

static int put_table_description(wire_writer_t *out, const void *context)
{
    const table_t *table = context;
    size_t count = table_column_count(table);
    protocol_column_t columns[TABLE_MAX_COLUMNS];
    for (size_t i = 0; i < count; i++) {
        const table_column_t *column = table_column(table, table_physical_index(table, i));
        columns[i] = (protocol_column_t){column->name, type_oid(column->type)};
    }
    return protocol_put_row_description(out, columns, count);
}

static int put_table_row(wire_writer_t *out, const table_t *table, size_t row)
{
    size_t count = table_column_count(table);
    protocol_value_t values[TABLE_MAX_COLUMNS];
    for (size_t i = 0; i < count; i++) {
        const char *cell = table_cell(table, row, table_physical_index(table, i));
        values[i] = (protocol_value_t){cell, strlen(cell)};
    }
    return protocol_put_data_row(out, values, count);
}

static int put_table_rows(wire_writer_t *out, const void *context)
{
    const table_t *table = context;
    int result = 0;
    for (size_t row = 0; row < table_row_count(table) && result == 0; row++) {
        result = put_table_row(out, table, row);
    }
    return result;
}

static int put_table_tag(wire_writer_t *out, const void *context)
{
    char tag[TAG_CAPACITY];
    (void) snprintf(tag, sizeof(tag), SELECT_TAG_FORMAT, table_row_count(context));
    return protocol_put_command_complete(out, tag);
}

static int put_insert_tag(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_command_complete(out, INSERT_TAG);
}

static int put_create_table_tag(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_command_complete(out, CREATE_TABLE_TAG);
}

static int put_executor_error(wire_writer_t *out, const void *context)
{
    const executor_error_t *error = context;
    return protocol_put_error_response(out, ERROR_SEVERITY, error->sqlstate, error->message);
}

static int put_empty_query(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_empty_query_response(out);
}

static int put_ready(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_ready_for_query(out, TRANSACTION_IDLE);
}

static const response_step_t SINGLE_VALUE_RESPONSE[] = {put_single_description, put_single_row,
                                                        put_select_one_tag, put_ready};
static const response_step_t TABLE_RESPONSE[] = {put_table_description, put_table_rows,
                                                 put_table_tag, put_ready};
static const response_step_t INSERT_RESPONSE[] = {put_insert_tag, put_ready};
static const response_step_t EMPTY_RESPONSE[] = {put_empty_query, put_ready};
static const response_step_t CREATE_TABLE_RESPONSE[] = {put_create_table_tag, put_ready};
static const response_step_t EXECUTOR_ERROR_RESPONSE[] = {put_executor_error, put_ready};
static const response_step_t UNSUPPORTED_RESPONSE[] = {put_unsupported_error, put_ready};

#define STEP_COUNT(steps) (sizeof(steps) / sizeof((steps)[0]))

static int run_steps(wire_writer_t *out, const response_step_t *steps, size_t count,
                     const void *context)
{
    int result = 0;
    for (size_t i = 0; i < count && result == 0; i++) {
        result = steps[i](out, context);
    }
    return result;
}

static int respond_error(const executor_error_t *error, wire_writer_t *out)
{
    return run_steps(out, EXECUTOR_ERROR_RESPONSE, STEP_COUNT(EXECUTOR_ERROR_RESPONSE), error);
}

static int respond_select_all(const char *table_name, wire_writer_t *out)
{
    executor_select_t select;
    executor_select_all(table_name, &select);
    int result = select.table == nullptr
                     ? respond_error(&select.error, out)
                     : run_steps(out, TABLE_RESPONSE, STEP_COUNT(TABLE_RESPONSE), select.table);
    executor_select_release(&select);
    return result;
}

static int respond_insert(const sql_insert_t *insert, wire_writer_t *out)
{
    executor_error_t error;
    if (executor_insert(insert, &error) != 0) {
        return -1;
    }
    if (error.sqlstate[0] != '\0') {
        return respond_error(&error, out);
    }
    return run_steps(out, INSERT_RESPONSE, STEP_COUNT(INSERT_RESPONSE), nullptr);
}

// Returns whether sql is an INSERT; if so, *result is the outcome of answering it
static bool respond_if_insert(const char *sql, wire_writer_t *out, int *result)
{
    sql_insert_t insert;
    char *scratch = malloc(strlen(sql) + 1);
    if (scratch == nullptr) {
        *result = -1;
        return true;
    }
    bool is_insert = sql_parse_insert(sql, scratch, &insert);
    if (is_insert) {
        *result = respond_insert(&insert, out);
    }
    free(scratch);
    return is_insert;
}

static int respond_create_table(const sql_create_table_t *create, wire_writer_t *out)
{
    executor_error_t error;
    if (executor_create_table(create, &error) != 0) {
        return -1;
    }
    if (error.sqlstate[0] != '\0') {
        return respond_error(&error, out);
    }
    return run_steps(out, CREATE_TABLE_RESPONSE, STEP_COUNT(CREATE_TABLE_RESPONSE), nullptr);
}

static int respond_single_value(wire_writer_t *out, const char *column, const char *value)
{
    const single_value_t single = {column, value};
    return run_steps(out, SINGLE_VALUE_RESPONSE, STEP_COUNT(SINGLE_VALUE_RESPONSE), &single);
}

// Returns whether sql is one of the single-value queries; if so, *result is the outcome
static bool respond_if_single_value(const char *sql, const session_info_t *session,
                                    wire_writer_t *out, int *result)
{
    if (sql_is_version_query(sql)) {
        *result = respond_single_value(out, VERSION_COLUMN, SERVER_VERSION);
    } else if (sql_is_current_user_query(sql)) {
        *result = respond_single_value(out, CURRENT_USER_COLUMN, session->user);
    } else if (sql_is_current_database_query(sql)) {
        *result = respond_single_value(out, CURRENT_DATABASE_COLUMN, session->database);
    } else {
        return false;
    }
    return true;
}

static int respond(const char *sql, const session_info_t *session, wire_writer_t *out)
{
    int result = 0;
    sql_create_table_t create;
    char table_name[SQL_IDENTIFIER_CAPACITY];
    if (sql_is_empty(sql)) {
        return run_steps(out, EMPTY_RESPONSE, STEP_COUNT(EMPTY_RESPONSE), nullptr);
    }
    if (respond_if_single_value(sql, session, out, &result)) {
        return result;
    }
    if (sql_parse_select_all(sql, table_name, sizeof(table_name))) {
        return respond_select_all(table_name, out);
    }
    if (respond_if_insert(sql, out, &result)) {
        return result;
    }
    if (sql_parse_create_table(sql, &create)) {
        return respond_create_table(&create, out);
    }
    return run_steps(out, UNSUPPORTED_RESPONSE, STEP_COUNT(UNSUPPORTED_RESPONSE), nullptr);
}

static int read_sql(const void *body, size_t size, const char **sql)
{
    wire_reader_t *reader = wire_reader_create(body, size);
    if (reader == nullptr) {
        return -1;
    }
    int status = wire_reader_get_cstring(reader, sql);
    wire_reader_destroy(reader);
    return status;
}

int query_handle_message(const void *body, size_t size, wire_writer_t *out,
                         const session_info_t *session)
{
    const char *sql = nullptr;
    if (out == nullptr || session == nullptr) {
        errno = EINVAL;
        return -1;
    }
    if (read_sql(body, size, &sql) != 0) {
        return -1;
    }
    size_t start = wire_writer_size(out);
    int status = respond(sql, session, out);
    if (status != 0) {
        wire_writer_truncate(out, start);
    }
    return status;
}
