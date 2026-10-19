/**
 * The row codec: how the values of one row are laid out as bytes.
 *
 * A row is a null bitmap followed by the values of the columns that are not NULL, in column order.
 * Bit i of the bitmap (bit i % 8 of byte i / 8) is set when column i is NULL; a NULL takes no other
 * space. A value of a fixed-size type is its storage_size bytes. A value of a variable-length
 * type has a 16-bit length in front of its bytes. The top bit of that length is reserved for the
 * out-of-line storage of large values (Phase 8+) and must be zero, so a value is at most
 * ROW_MAX_VALUE_LENGTH bytes. A row with no columns has no bytes at all.
 *
 * The values are in the stored format of their types (see type_registry.h), and the codec does not
 * look inside them. It knows nothing about pages, tuple headers or TIDs, so any storage engine can
 * use it. Rows are encoded and decoded in the machine's byte order, like the values themselves.
 *
 * A value is a pointer and a length; the pointer is nullptr only for NULL (an empty text has a
 * pointer to anywhere, and length 0). The values that row_decode returns point into the row.
 *
 * Errors return -1 and set errno: EINVAL for a value whose length is not the size of its fixed-size
 * type, and for a row that row_decode finds malformed (too short, too long, or with the reserved
 * bit set); EFBIG for a value longer than ROW_MAX_VALUE_LENGTH; E2BIG when the row does not fit the
 * buffer of row_encode.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "types/type_registry.h"

/** The longest value of a variable-length type. */
constexpr size_t ROW_MAX_VALUE_LENGTH = INT16_MAX;

typedef struct {
    const void *data;
    size_t length;
} row_value_t;

/** The types of the columns, in column order. */
typedef struct {
    const type_info_t *const *types;
    size_t count;
} row_layout_t;

/** The number of bytes that row_encode needs for these values. */
[[nodiscard]] ssize_t row_size(const row_layout_t *layout, const row_value_t *values);

/** Writes the row; returns its size. */
[[nodiscard]] ssize_t row_encode(const row_layout_t *layout, const row_value_t *values,
                                 void *buffer, size_t capacity);

/** Fills values (one per column) from a row of exactly length bytes. */
[[nodiscard]] int row_decode(const row_layout_t *layout, const void *row, size_t length,
                             row_value_t *values);
