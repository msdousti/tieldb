/**
 * Output buffer primitives for the Postgres wire protocol.
 *
 * Why this exists
 * ---------------
 * psql and TielDB talk over TCP using the Postgres wire protocol. Almost every
 * message is built from the same few primitives, so this module writes them
 * one by one into a growing byte buffer.
 *
 * Anatomy of a message
 * --------------------
 * A message is a one-byte type tag, a four-byte length, then a body:
 *
 *      1 byte        4 bytes                  (length - 4) bytes
 *   +------------+----------------------+---------------------------+
 *   |    type    |        length        |           body            |
 *   |   'Q' etc  |  big-endian, signed  |  fields, in message order |
 *   +------------+----------------------+---------------------------+
 *                 \_____ counts itself and the body, NOT the type byte _____/
 *
 * Example: a client asking the server to run "SELECT 1" sends
 *
 *   byte index:   0     1  2  3  4    5  6  7  8  9 10 11 12   13
 *   hex:         51    00 00 00 0D   53 45 4C 45 43 54 20 31   00
 *   meaning:    'Q'    length = 13   S  E  L  E  C  T  .  1    NUL
 *                      (4 + 8 + 1)   \_______ string _______/  end
 *
 * The startup message that opens a connection is the one exception: it has
 * no type tag, only the length and a body.
 *
 * Field types
 * -----------
 * The protocol documentation names each field type; this is how they map:
 *
 *   Protocol type   Meaning                           Writer function
 *   -------------   -------------------------------   ----------------------
 *   Byte1           one raw byte (e.g. type tag)      wire_writer_put_u8
 *   Int16           16-bit integer, signed            wire_writer_put_i16
 *   Int32           32-bit integer, signed            wire_writer_put_i32
 *   Byten           n raw bytes                       wire_writer_put_bytes
 *   String          text followed by a 0 byte (NUL)   wire_writer_put_cstring
 *
 * Integers are big-endian ("network order"): the most significant byte goes
 * first, so 0x01020304 is written as 01 02 03 04.
 *
 * Integers are signed because the protocol uses negative values as markers.
 * For example, a column value in a result row is preceded by its length, and
 * a length of -1 means the value is SQL NULL. Raw bytes carry no sign, so
 * Byte1 and Byten use unsigned types.
 *
 * Error handling
 * --------------
 * All functions return 0 on success and -1 on failure; a failed call leaves
 * the buffer unchanged.
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
