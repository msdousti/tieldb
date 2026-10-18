/**
 * Handler for the simple Query message ('Q'), hardcoded for now.
 */

#include "net/query_handler.h"

#include <errno.h>
#include <string.h>

#include "sql/simple_sql.h"

static constexpr char SERVER_VERSION[] = "TielDB 0.1.0";
static constexpr char VERSION_COLUMN[] = "version";
static constexpr char SELECT_ONE_TAG[] = "SELECT 1";
static constexpr char ERROR_SEVERITY[] = "ERROR";
static constexpr char UNSUPPORTED_SQLSTATE[] = "0A000";
static constexpr char UNSUPPORTED_MESSAGE[] = "only SELECT version() is supported";

typedef int (*response_step_t)(wire_writer_t *out);

static int put_version_description(wire_writer_t *out)
{
    const protocol_column_t column = {VERSION_COLUMN, PROTOCOL_TYPE_TEXT};
    return protocol_put_row_description(out, &column, 1);
}

static int put_version_row(wire_writer_t *out)
{
    const protocol_value_t value = {SERVER_VERSION, sizeof(SERVER_VERSION) - 1};
    return protocol_put_data_row(out, &value, 1);
}

static int put_select_tag(wire_writer_t *out)
{
    return protocol_put_command_complete(out, SELECT_ONE_TAG);
}

static int put_unsupported_error(wire_writer_t *out)
{
    return protocol_put_error_response(out, ERROR_SEVERITY, UNSUPPORTED_SQLSTATE,
                                       UNSUPPORTED_MESSAGE);
}

static int put_ready(wire_writer_t *out)
{
    return protocol_put_ready_for_query(out, TRANSACTION_IDLE);
}

static const response_step_t VERSION_RESPONSE[] = {put_version_description, put_version_row,
                                                   put_select_tag, put_ready};
static const response_step_t UNSUPPORTED_RESPONSE[] = {put_unsupported_error, put_ready};

static int run_steps(wire_writer_t *out, const response_step_t *steps, size_t count)
{
    int result = 0;
    for (size_t i = 0; i < count && result == 0; i++) {
        result = steps[i](out);
    }
    return result;
}

static int respond(const char *sql, wire_writer_t *out)
{
    if (sql_is_version_query(sql)) {
        return run_steps(out, VERSION_RESPONSE,
                         sizeof(VERSION_RESPONSE) / sizeof(VERSION_RESPONSE[0]));
    }
    return run_steps(out, UNSUPPORTED_RESPONSE,
                     sizeof(UNSUPPORTED_RESPONSE) / sizeof(UNSUPPORTED_RESPONSE[0]));
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
