#include "storage/row_codec.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

typedef uint16_t value_length_t;

static constexpr value_length_t RESERVED_LENGTH_BIT = 1U
                                                      << ((sizeof(value_length_t) * CHAR_BIT) - 1);

static size_t bitmap_size(size_t count)
{
    return (count + CHAR_BIT - 1) / CHAR_BIT;
}

static bool is_null_bit_set(const unsigned char *bitmap, size_t column)
{
    return ((bitmap[column / CHAR_BIT] >> (column % CHAR_BIT)) & 1U) != 0;
}

static bool is_variable_length(const type_info_t *type)
{
    return type->storage_size == TYPE_VARIABLE_LENGTH;
}

static ssize_t fail_with(int error)
{
    errno = error;
    return -1;
}

static ssize_t value_size(const type_info_t *type, const row_value_t *value)
{
    if (value->data == nullptr) {
        return 0;
    }
    if (!is_variable_length(type)) {
        return value->length == type->storage_size ? (ssize_t) value->length : fail_with(EINVAL);
    }
    if (value->length > ROW_MAX_VALUE_LENGTH) {
        return fail_with(EFBIG);
    }
    return (ssize_t) (sizeof(value_length_t) + value->length);
}

ssize_t row_size(const row_layout_t *layout, const row_value_t *values)
{
    ssize_t total = (ssize_t) bitmap_size(layout->count);
    for (size_t column = 0; column < layout->count && total >= 0; column++) {
        ssize_t size = value_size(layout->types[column], &values[column]);
        total = size < 0 ? -1 : total + size;
    }
    return total;
}

typedef struct {
    unsigned char *bytes;
    size_t position;
} row_writer_t;

static void put(row_writer_t *writer, const void *data, size_t size)
{
    memcpy(writer->bytes + writer->position, data, size);
    writer->position += size;
}

static void put_value(row_writer_t *writer, const type_info_t *type, const row_value_t *value)
{
    if (value->data == nullptr) {
        return;
    }
    if (is_variable_length(type)) {
        value_length_t length = (value_length_t) value->length;
        put(writer, &length, sizeof(length));
    }
    put(writer, value->data, value->length);
}

static void put_null_bitmap(row_writer_t *writer, const row_layout_t *layout,
                            const row_value_t *values)
{
    memset(writer->bytes, 0, bitmap_size(layout->count));
    for (size_t column = 0; column < layout->count; column++) {
        writer->bytes[column / CHAR_BIT] |=
            (unsigned char) ((values[column].data == nullptr ? 1U : 0U) << (column % CHAR_BIT));
    }
    writer->position = bitmap_size(layout->count);
}

ssize_t row_encode(const row_layout_t *layout, const row_value_t *values, void *buffer,
                   size_t capacity)
{
    ssize_t size = row_size(layout, values);
    if (size < 0) {
        return -1;
    }
    if ((size_t) size > capacity) {
        return fail_with(E2BIG);
    }
    row_writer_t writer = {buffer, 0};
    put_null_bitmap(&writer, layout, values);
    for (size_t column = 0; column < layout->count; column++) {
        put_value(&writer, layout->types[column], &values[column]);
    }
    return size;
}

typedef struct {
    const unsigned char *bytes;
    size_t length;
    size_t position;
} row_reader_t;

static bool take(row_reader_t *reader, size_t size, const unsigned char **taken)
{
    if (size > reader->length - reader->position) {
        errno = EINVAL;
        return false;
    }
    *taken = reader->bytes + reader->position;
    reader->position += size;
    return true;
}

static bool take_value_size(row_reader_t *reader, const type_info_t *type, size_t *size)
{
    const unsigned char *prefix = nullptr;
    value_length_t length = 0;
    if (!is_variable_length(type)) {
        *size = type->storage_size;
        return true;
    }
    if (!take(reader, sizeof(length), &prefix)) {
        return false;
    }
    memcpy(&length, prefix, sizeof(length));
    *size = length;
    if ((length & RESERVED_LENGTH_BIT) != 0) {
        errno = EINVAL;
        return false;
    }
    return true;
}

static bool take_value(row_reader_t *reader, const type_info_t *type, row_value_t *value)
{
    size_t size = 0;
    const unsigned char *data = nullptr;
    if (!take_value_size(reader, type, &size) || !take(reader, size, &data)) {
        return false;
    }
    *value = (row_value_t){data, size};
    return true;
}

static bool take_column(row_reader_t *reader, const type_info_t *type, bool is_null,
                        row_value_t *value)
{
    *value = (row_value_t){};
    return is_null || take_value(reader, type, value);
}

int row_decode(const row_layout_t *layout, const void *row, size_t length, row_value_t *values)
{
    row_reader_t reader = {row, length, 0};
    const unsigned char *bitmap = nullptr;
    bool is_valid = take(&reader, bitmap_size(layout->count), &bitmap);
    for (size_t column = 0; is_valid && column < layout->count; column++) {
        is_valid = take_column(&reader, layout->types[column], is_null_bit_set(bitmap, column),
                               &values[column]);
    }
    if (is_valid && reader.position != length) {
        errno = EINVAL;
        is_valid = false;
    }
    return is_valid ? 0 : -1;
}
