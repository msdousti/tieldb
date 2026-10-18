/**
 * The type registry: what is known about each SQL type, as data, like Postgres's pg_type.
 *
 * A type has a name, an OID (the same number the wire protocol sends), a storage size, and three
 * functions: input turns text into the stored form, output turns the stored form back into text,
 * and compare orders two stored values. A stored value is a byte string; a type with a fixed size
 * always stores exactly that many bytes, a type of variable length stores any number.
 *
 * Integers are stored as 4 bytes in the machine's byte order, so "007" and "7" are the same value.
 * Text is stored without a terminating zero and compares byte by byte.
 *
 * The registry is read-only after startup, so any thread may use it without locking. Types are
 * found by name (including aliases such as "int" and "int4", in any letter case) or by OID.
 *
 * Errors: the lookups return nullptr with errno ENOENT when the type is unknown. The input
 * functions return -1 with errno EINVAL when the text is not a valid value of the type
 * and E2BIG when the stored form does not fit the buffer. The output functions return -1 with
 * errno E2BIG when the text, with its terminating zero, does not fit the buffer.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t type_oid_t;

/** Postgres type OIDs of the registered types. */
constexpr type_oid_t TYPE_OID_INT4 = 23;
constexpr type_oid_t TYPE_OID_TEXT = 25;

/** The storage size of a type whose values have any length. */
constexpr size_t TYPE_VARIABLE_LENGTH = 0;

typedef struct {
    type_oid_t oid;
    const char *name;
    size_t storage_size;
    /** Whether the text is a valid value of the type. */
    bool (*accepts)(const char *text);
    /** Converts text to the stored form; returns the number of bytes stored. */
    ssize_t (*input)(const char *text, void *buffer, size_t capacity);
    /** Converts a stored value to text with a terminating zero; returns the length without it. */
    ssize_t (*output)(const void *value, size_t length, char *buffer, size_t capacity);
    /** Negative, zero or positive when left is less than, equal to or greater than right. */
    int (*compare)(const void *left, size_t left_length, const void *right, size_t right_length);
} type_info_t;

extern const type_info_t TYPE_INT4;
extern const type_info_t TYPE_TEXT;

[[nodiscard]] const type_info_t *type_find_by_name(const char *name);
[[nodiscard]] const type_info_t *type_find_by_oid(type_oid_t oid);
