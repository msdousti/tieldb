/**
 * Byte buffer primitives for the Postgres wire protocol.
 */

#include "net/wire_buffer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t WRITER_INITIAL_CAPACITY = 64;
static constexpr size_t WRITER_MAX_SIZE = INT32_MAX;

static_assert(WRITER_MAX_SIZE <= SIZE_MAX / 2);

struct wire_writer {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

wire_writer_t *wire_writer_create(void)
{
    wire_writer_t *writer = calloc(1, sizeof(*writer));
    if (writer == nullptr) {
        return nullptr;
    }
    writer->data = malloc(WRITER_INITIAL_CAPACITY);
    if (writer->data == nullptr) {
        free(writer);
        return nullptr;
    }
    writer->capacity = WRITER_INITIAL_CAPACITY;
    return writer;
}

void wire_writer_destroy(wire_writer_t *writer)
{
    if (writer == nullptr) {
        return;
    }
    free(writer->data);
    writer->data = nullptr;
    free(writer);
}

static int fail_with(int error)
{
    errno = error;
    return -1;
}

static size_t grown_capacity(size_t capacity, size_t needed)
{
    while (capacity < needed) {
        capacity *= 2;
    }
    return capacity;
}

static int writer_reserve(wire_writer_t *writer, size_t extra)
{
    if (extra > WRITER_MAX_SIZE - writer->size) {
        return fail_with(EOVERFLOW);
    }
    size_t capacity = grown_capacity(writer->capacity, writer->size + extra);
    if (capacity == writer->capacity) {
        return 0;
    }
    uint8_t *grown = realloc(writer->data, capacity);
    if (grown == nullptr) {
        return fail_with(ENOMEM);
    }
    writer->data = grown;
    writer->capacity = capacity;
    return 0;
}

int wire_writer_put_bytes(wire_writer_t *writer, const void *data, size_t size)
{
    if (writer == nullptr || (data == nullptr && size > 0)) {
        return fail_with(EINVAL);
    }
    if (writer_reserve(writer, size) != 0) {
        return -1;
    }
    if (size > 0) {
        memcpy(writer->data + writer->size, data, size);
        writer->size += size;
    }
    return 0;
}

int wire_writer_put_u8(wire_writer_t *writer, uint8_t value)
{
    return wire_writer_put_bytes(writer, &value, sizeof(value));
}

void wire_encode_i16(uint8_t *out, int16_t value)
{
    uint16_t bits = (uint16_t) value;
    out[0] = (uint8_t) (bits >> 8);
    out[1] = (uint8_t) bits;
}

void wire_encode_i32(uint8_t *out, int32_t value)
{
    uint32_t bits = (uint32_t) value;
    out[0] = (uint8_t) (bits >> 24);
    out[1] = (uint8_t) (bits >> 16);
    out[2] = (uint8_t) (bits >> 8);
    out[3] = (uint8_t) bits;
}

int wire_writer_put_i16(wire_writer_t *writer, int16_t value)
{
    uint8_t bytes[2];
    wire_encode_i16(bytes, value);
    return wire_writer_put_bytes(writer, bytes, sizeof(bytes));
}

int wire_writer_put_i32(wire_writer_t *writer, int32_t value)
{
    uint8_t bytes[4];
    wire_encode_i32(bytes, value);
    return wire_writer_put_bytes(writer, bytes, sizeof(bytes));
}

int wire_writer_patch_i32(wire_writer_t *writer, size_t offset, int32_t value)
{
    if (writer == nullptr || writer->size < sizeof(int32_t) ||
        offset > writer->size - sizeof(int32_t)) {
        return fail_with(EINVAL);
    }
    wire_encode_i32(writer->data + offset, value);
    return 0;
}

void wire_writer_truncate(wire_writer_t *writer, size_t size)
{
    if (writer != nullptr && size < writer->size) {
        writer->size = size;
    }
}

int wire_writer_put_cstring(wire_writer_t *writer, const char *str)
{
    if (str == nullptr) {
        return fail_with(EINVAL);
    }
    return wire_writer_put_bytes(writer, str, strlen(str) + 1);
}

const uint8_t *wire_writer_data(const wire_writer_t *writer)
{
    return writer->data;
}

size_t wire_writer_size(const wire_writer_t *writer)
{
    return writer->size;
}

struct wire_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
};

wire_reader_t *wire_reader_create(const void *data, size_t size)
{
    if (data == nullptr && size > 0) {
        errno = EINVAL;
        return nullptr;
    }
    wire_reader_t *reader = calloc(1, sizeof(*reader));
    if (reader == nullptr) {
        return nullptr;
    }
    reader->data = data;
    reader->size = size;
    return reader;
}

void wire_reader_destroy(wire_reader_t *reader)
{
    free(reader);
}

size_t wire_reader_remaining(const wire_reader_t *reader)
{
    if (reader == nullptr) {
        return 0;
    }
    return reader->size - reader->pos;
}

int wire_reader_get_bytes(wire_reader_t *reader, void *out, size_t size)
{
    if (reader == nullptr || (out == nullptr && size > 0)) {
        return fail_with(EINVAL);
    }
    if (size > wire_reader_remaining(reader)) {
        return fail_with(EBADMSG);
    }
    if (size > 0) {
        memcpy(out, reader->data + reader->pos, size);
        reader->pos += size;
    }
    return 0;
}

int wire_reader_get_u8(wire_reader_t *reader, uint8_t *out)
{
    return wire_reader_get_bytes(reader, out, sizeof(uint8_t));
}

int wire_reader_get_i16(wire_reader_t *reader, int16_t *out)
{
    uint8_t bytes[2];
    if (out == nullptr) {
        return fail_with(EINVAL);
    }
    if (wire_reader_get_bytes(reader, bytes, sizeof(bytes)) != 0) {
        return -1;
    }
    *out = (int16_t) (uint16_t) ((bytes[0] << 8) | bytes[1]);
    return 0;
}

int wire_reader_get_i32(wire_reader_t *reader, int32_t *out)
{
    uint8_t bytes[4];
    if (out == nullptr) {
        return fail_with(EINVAL);
    }
    if (wire_reader_get_bytes(reader, bytes, sizeof(bytes)) != 0) {
        return -1;
    }
    *out = (int32_t) (((uint32_t) bytes[0] << 24) | ((uint32_t) bytes[1] << 16) |
                      ((uint32_t) bytes[2] << 8) | (uint32_t) bytes[3]);
    return 0;
}

int wire_reader_get_cstring(wire_reader_t *reader, const char **out)
{
    if (reader == nullptr || out == nullptr) {
        return fail_with(EINVAL);
    }
    if (wire_reader_remaining(reader) == 0) {
        return fail_with(EBADMSG);
    }
    const uint8_t *start = reader->data + reader->pos;
    const uint8_t *end = memchr(start, 0, wire_reader_remaining(reader));
    if (end == nullptr) {
        return fail_with(EBADMSG);
    }
    *out = (const char *) start;
    reader->pos += (size_t) (end - start) + 1;
    return 0;
}
