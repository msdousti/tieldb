/**
 * Encoders for the Postgres backend (server to client) messages.
 */

#include "net/protocol.h"

#include <errno.h>
#include <string.h>

static constexpr size_t SQLSTATE_LENGTH = 5;

// We have no table OIDs or column numbers to report, so those two leading fields stay zero
static constexpr size_t COLUMN_TYPE_OID_OFFSET = sizeof(int32_t) + sizeof(int16_t);
static constexpr size_t COLUMN_TYPE_SIZE_OFFSET = COLUMN_TYPE_OID_OFFSET + sizeof(int32_t);
static constexpr size_t COLUMN_TYPE_MODIFIER_OFFSET = COLUMN_TYPE_SIZE_OFFSET + sizeof(int16_t);
static constexpr size_t COLUMN_FIXED_SIZE =
    COLUMN_TYPE_MODIFIER_OFFSET + sizeof(int32_t) + sizeof(int16_t);
static constexpr int16_t VARIABLE_TYPE_SIZE = -1;
static constexpr int32_t NULL_VALUE_LENGTH = -1;
static constexpr int32_t NO_TYPE_MODIFIER = -1;

typedef struct error_field {
    char code;
    const char *value;
} error_field_t;

static int begin_message(wire_writer_t *writer, uint8_t type)
{
    const uint8_t header[] = {type, 0, 0, 0, 0};
    return wire_writer_put_bytes(writer, header, sizeof(header));
}

// Cannot fail: start was just written, and the writer never exceeds INT32_MAX bytes, so the
// length fits an int32. Callers therefore need no rollback for it.
static int end_message(wire_writer_t *writer, size_t start)
{
    size_t length = wire_writer_size(writer) - start - 1;
    return wire_writer_patch_i32(writer, start + 1, (int32_t) length);
}

int protocol_put_authentication_ok(wire_writer_t *writer)
{
    static const uint8_t message[] = {'R', 0, 0, 0, 8, 0, 0, 0, 0};
    return wire_writer_put_bytes(writer, message, sizeof(message));
}

int protocol_put_parameter_status(wire_writer_t *writer, const char *name, const char *value)
{
    if (writer == nullptr || name == nullptr || value == nullptr) {
        errno = EINVAL;
        return -1;
    }
    size_t start = wire_writer_size(writer);
    if (begin_message(writer, 'S') != 0 || wire_writer_put_cstring(writer, name) != 0 ||
        wire_writer_put_cstring(writer, value) != 0) {
        wire_writer_truncate(writer, start);
        return -1;
    }
    return end_message(writer, start);
}

int protocol_put_empty_query_response(wire_writer_t *writer)
{
    static const uint8_t message[] = {'I', 0, 0, 0, 4};
    return wire_writer_put_bytes(writer, message, sizeof(message));
}

int protocol_put_ready_for_query(wire_writer_t *writer, transaction_status_t status)
{
    if (status != TRANSACTION_IDLE && status != TRANSACTION_IN_BLOCK &&
        status != TRANSACTION_FAILED) {
        errno = EINVAL;
        return -1;
    }
    const uint8_t message[] = {'Z', 0, 0, 0, 5, (uint8_t) status};
    return wire_writer_put_bytes(writer, message, sizeof(message));
}

int protocol_put_command_complete(wire_writer_t *writer, const char *tag)
{
    if (writer == nullptr || tag == nullptr) {
        errno = EINVAL;
        return -1;
    }
    size_t start = wire_writer_size(writer);
    if (begin_message(writer, 'C') != 0 || wire_writer_put_cstring(writer, tag) != 0) {
        wire_writer_truncate(writer, start);
        return -1;
    }
    return end_message(writer, start);
}

static int put_error_field(wire_writer_t *writer, const error_field_t *field)
{
    if (wire_writer_put_u8(writer, (uint8_t) field->code) != 0) {
        return -1;
    }
    return wire_writer_put_cstring(writer, field->value);
}

static int put_error_fields(wire_writer_t *writer, const error_field_t *fields, size_t count)
{
    int result = 0;
    for (size_t i = 0; i < count && result == 0; i++) {
        result = put_error_field(writer, &fields[i]);
    }
    if (result != 0) {
        return -1;
    }
    return wire_writer_put_u8(writer, 0);
}

int protocol_put_error_response(wire_writer_t *writer, const char *severity, const char *sqlstate,
                                const char *message)
{
    if (writer == nullptr || severity == nullptr || sqlstate == nullptr || message == nullptr ||
        strlen(sqlstate) != SQLSTATE_LENGTH) {
        errno = EINVAL;
        return -1;
    }
    const error_field_t fields[] = {{'S', severity}, {'C', sqlstate}, {'M', message}};
    size_t start = wire_writer_size(writer);
    if (begin_message(writer, 'E') != 0 ||
        put_error_fields(writer, fields, sizeof(fields) / sizeof(fields[0])) != 0) {
        wire_writer_truncate(writer, start);
        return -1;
    }
    return end_message(writer, start);
}

static int column_type_size(int32_t type_oid, int16_t *type_size)
{
    switch (type_oid) {
        case PROTOCOL_TYPE_INT4:
            *type_size = sizeof(int32_t);
            return 0;
        case PROTOCOL_TYPE_TEXT:
            *type_size = VARIABLE_TYPE_SIZE;
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int put_column(wire_writer_t *writer, const protocol_column_t *column)
{
    uint8_t fixed[COLUMN_FIXED_SIZE] = {};
    int16_t type_size = 0;
    if (column_type_size(column->type_oid, &type_size) != 0) {
        return -1;
    }
    wire_encode_i32(fixed + COLUMN_TYPE_OID_OFFSET, column->type_oid);
    wire_encode_i16(fixed + COLUMN_TYPE_SIZE_OFFSET, type_size);
    wire_encode_i32(fixed + COLUMN_TYPE_MODIFIER_OFFSET, NO_TYPE_MODIFIER);
    if (wire_writer_put_cstring(writer, column->name) != 0) {
        return -1;
    }
    return wire_writer_put_bytes(writer, fixed, sizeof(fixed));
}

static int put_columns(wire_writer_t *writer, const protocol_column_t *columns, size_t count)
{
    int result = 0;
    for (size_t i = 0; i < count && result == 0; i++) {
        result = put_column(writer, &columns[i]);
    }
    return result;
}

int protocol_put_row_description(wire_writer_t *writer, const protocol_column_t *columns,
                                 size_t count)
{
    if (writer == nullptr || count > INT16_MAX || (columns == nullptr && count > 0)) {
        errno = EINVAL;
        return -1;
    }
    size_t start = wire_writer_size(writer);
    if (begin_message(writer, 'T') != 0 || wire_writer_put_i16(writer, (int16_t) count) != 0 ||
        put_columns(writer, columns, count) != 0) {
        wire_writer_truncate(writer, start);
        return -1;
    }
    return end_message(writer, start);
}

static int put_value(wire_writer_t *writer, const protocol_value_t *value)
{
    bool has_length_without_data = value->data == nullptr && value->length > 0;
    if (has_length_without_data || value->length > INT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (value->data == nullptr) {
        return wire_writer_put_i32(writer, NULL_VALUE_LENGTH);
    }
    if (wire_writer_put_i32(writer, (int32_t) value->length) != 0) {
        return -1;
    }
    return wire_writer_put_bytes(writer, value->data, value->length);
}

static int put_values(wire_writer_t *writer, const protocol_value_t *values, size_t count)
{
    int result = 0;
    for (size_t i = 0; i < count && result == 0; i++) {
        result = put_value(writer, &values[i]);
    }
    return result;
}

int protocol_put_data_row(wire_writer_t *writer, const protocol_value_t *values, size_t count)
{
    if (writer == nullptr || count > INT16_MAX || (values == nullptr && count > 0)) {
        errno = EINVAL;
        return -1;
    }
    size_t start = wire_writer_size(writer);
    if (begin_message(writer, 'D') != 0 || wire_writer_put_i16(writer, (int16_t) count) != 0 ||
        put_values(writer, values, count) != 0) {
        wire_writer_truncate(writer, start);
        return -1;
    }
    return end_message(writer, start);
}
