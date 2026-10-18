/**
 * Output buffer primitives for the Postgres wire protocol.
 */

#include "net/wire_buffer.h"

#include <stdlib.h>
#include <string.h>

enum { WRITER_INITIAL_CAPACITY = 64 };

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

static int writer_reserve(wire_writer_t *writer, size_t extra)
{
    if (extra > SIZE_MAX - writer->size) {
        return -1;
    }
    size_t needed = writer->size + extra;
    size_t capacity = writer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            return -1;
        }
        capacity *= 2;
    }
    if (capacity == writer->capacity) {
        return 0;
    }
    uint8_t *grown = realloc(writer->data, capacity);
    if (grown == nullptr) {
        return -1;
    }
    writer->data = grown;
    writer->capacity = capacity;
    return 0;
}

int wire_writer_put_bytes(wire_writer_t *writer, const void *data, size_t size)
{
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

int wire_writer_put_i16(wire_writer_t *writer, int16_t value)
{
    uint16_t bits = (uint16_t) value;
    const uint8_t bytes[] = {(uint8_t) (bits >> 8), (uint8_t) bits};
    return wire_writer_put_bytes(writer, bytes, sizeof(bytes));
}

int wire_writer_put_i32(wire_writer_t *writer, int32_t value)
{
    uint32_t bits = (uint32_t) value;
    const uint8_t bytes[] = {(uint8_t) (bits >> 24), (uint8_t) (bits >> 16), (uint8_t) (bits >> 8),
                             (uint8_t) bits};
    return wire_writer_put_bytes(writer, bytes, sizeof(bytes));
}

int wire_writer_put_cstring(wire_writer_t *writer, const char *str)
{
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
