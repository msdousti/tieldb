/**
 * Handler for the simple Query message ('Q'), hardcoded for now.
 */

#include "net/query_handler.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sql/simple_sql.h"
#include "storage/table.h"

static constexpr char SERVER_VERSION[] = "TielDB 0.1.0";
static constexpr char VERSION_COLUMN[] = "version";
static constexpr char SELECT_ONE_TAG[] = "SELECT 1";
static constexpr char ERROR_SEVERITY[] = "ERROR";
static constexpr char UNSUPPORTED_SQLSTATE[] = "0A000";
static constexpr char UNSUPPORTED_MESSAGE[] =
    "only SELECT version() and SELECT * FROM <table> are supported";
static constexpr char MISSING_TABLE_SQLSTATE[] = "42P01";
static constexpr char MISSING_TABLE_FORMAT[] = "relation \"%s\" does not exist";
static constexpr char SELECT_TAG_FORMAT[] = "SELECT %zu";
static constexpr size_t TAG_CAPACITY = 32;
static constexpr size_t MESSAGE_CAPACITY = SQL_IDENTIFIER_CAPACITY + sizeof(MISSING_TABLE_FORMAT);

// Each step appends one message; context is the table or table name the response is about
typedef int (*response_step_t)(wire_writer_t *out, const void *context);

static int put_version_description(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    const protocol_column_t column = {VERSION_COLUMN, PROTOCOL_TYPE_TEXT};
    return protocol_put_row_description(out, &column, 1);
}

static int put_version_row(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    const protocol_value_t value = {SERVER_VERSION, sizeof(SERVER_VERSION) - 1};
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

static int put_missing_table_error(wire_writer_t *out, const void *context)
{
    char message[MESSAGE_CAPACITY];
    (void) snprintf(message, sizeof(message), MISSING_TABLE_FORMAT, (const char *) context);
    return protocol_put_error_response(out, ERROR_SEVERITY, MISSING_TABLE_SQLSTATE, message);
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
        const table_column_t *column = table_column(table, i);
        columns[i] = (protocol_column_t){column->name, type_oid(column->type)};
    }
    return protocol_put_row_description(out, columns, count);
}

static int put_table_row(wire_writer_t *out, const table_t *table, size_t row)
{
    size_t count = table_column_count(table);
    protocol_value_t values[TABLE_MAX_COLUMNS];
    for (size_t i = 0; i < count; i++) {
        const char *cell = table_cell(table, row, i);
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

static int put_ready(wire_writer_t *out, [[maybe_unused]] const void *context)
{
    return protocol_put_ready_for_query(out, TRANSACTION_IDLE);
}

static const response_step_t VERSION_RESPONSE[] = {put_version_description, put_version_row,
                                                   put_select_one_tag, put_ready};
static const response_step_t TABLE_RESPONSE[] = {put_table_description, put_table_rows,
                                                 put_table_tag, put_ready};
static const response_step_t MISSING_TABLE_RESPONSE[] = {put_missing_table_error, put_ready};
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

static int respond_select_all(const char *table_name, wire_writer_t *out)
{
    table_t *table = table_find(table_name);
    if (table == nullptr) {
        return run_steps(out, MISSING_TABLE_RESPONSE, STEP_COUNT(MISSING_TABLE_RESPONSE),
                         table_name);
    }
    table_lock(table);
    int result = run_steps(out, TABLE_RESPONSE, STEP_COUNT(TABLE_RESPONSE), table);
    table_unlock(table);
    return result;
}

static int respond(const char *sql, wire_writer_t *out)
{
    char table_name[SQL_IDENTIFIER_CAPACITY];
    if (sql_is_version_query(sql)) {
        return run_steps(out, VERSION_RESPONSE, STEP_COUNT(VERSION_RESPONSE), nullptr);
    }
    if (sql_parse_select_all(sql, table_name, sizeof(table_name))) {
        return respond_select_all(table_name, out);
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

int query_handle_message(const void *body, size_t size, wire_writer_t *out)
{
    const char *sql = nullptr;
    if (out == nullptr) {
        errno = EINVAL;
        return -1;
    }
    if (read_sql(body, size, &sql) != 0) {
        return -1;
    }
    size_t start = wire_writer_size(out);
    int status = respond(sql, out);
    if (status != 0) {
        wire_writer_truncate(out, start);
    }
    return status;
}
