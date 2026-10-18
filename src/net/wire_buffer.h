/**
 * Output buffer primitives for the Postgres wire protocol.
 *
 * Multi-byte integers are big-endian (network order). All functions return
 * 0 on success and -1 on failure; a failed call leaves the buffer unchanged.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** Growable output buffer. */
typedef struct wire_writer wire_writer_t;

/** Returns nullptr on allocation failure. */
wire_writer_t *wire_writer_create(void);
void wire_writer_destroy(wire_writer_t *writer);

[[nodiscard]] int wire_writer_put_u8(wire_writer_t *writer, uint8_t value);
[[nodiscard]] int wire_writer_put_i16(wire_writer_t *writer, int16_t value);
[[nodiscard]] int wire_writer_put_i32(wire_writer_t *writer, int32_t value);
[[nodiscard]] int wire_writer_put_bytes(wire_writer_t *writer, const void *data, size_t size);

/** Writes the string followed by its NUL terminator. */
[[nodiscard]] int wire_writer_put_cstring(wire_writer_t *writer, const char *str);

const uint8_t *wire_writer_data(const wire_writer_t *writer);
size_t wire_writer_size(const wire_writer_t *writer);
