#define _POSIX_C_SOURCE 200809L

#include "types/type_registry.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static constexpr int DECIMAL_BASE = 10;

static ssize_t fail_with(int error)
{
    errno = error;
    return -1;
}

static bool parse_int32(const char *text, int32_t *value)
{
    char *end = nullptr;
    errno = 0;
    long parsed = strtol(text, &end, DECIMAL_BASE);
    bool valid =
        end != text && *end == '\0' && errno == 0 && parsed >= INT32_MIN && parsed <= INT32_MAX;
    *value = (int32_t) parsed;
    return valid;
}

static ssize_t int4_input(const char *text, void *buffer, size_t capacity)
{
    int32_t value = 0;
    if (!parse_int32(text, &value)) {
        return fail_with(EINVAL);
    }
    if (capacity < sizeof(value)) {
        return fail_with(E2BIG);
    }
    memcpy(buffer, &value, sizeof(value));
    return sizeof(value);
}

static int32_t load_int32(const void *value)
{
    int32_t result = 0;
    memcpy(&result, value, sizeof(result));
    return result;
}

static ssize_t int4_output(const void *value, [[maybe_unused]] size_t length, char *buffer,
                           size_t capacity)
{
    int written = snprintf(buffer, capacity, "%d", load_int32(value));
    return (size_t) written >= capacity ? fail_with(E2BIG) : written;
}

static int int4_compare(const void *left, [[maybe_unused]] size_t left_length, const void *right,
                        [[maybe_unused]] size_t right_length)
{
    int32_t a = load_int32(left);
    int32_t b = load_int32(right);
    return (a > b) - (a < b);
}

static ssize_t text_input(const char *text, void *buffer, size_t capacity)
{
    size_t length = strlen(text);
    if (length > capacity) {
        return fail_with(E2BIG);
    }
    memcpy(buffer, text, length);
    return (ssize_t) length;
}

static ssize_t text_output(const void *value, size_t length, char *buffer, size_t capacity)
{
    if (length >= capacity) {
        return fail_with(E2BIG);
    }
    memcpy(buffer, value, length);
    buffer[length] = '\0';
    return (ssize_t) length;
}

static int text_compare(const void *left, size_t left_length, const void *right,
                        size_t right_length)
{
    size_t common = left_length < right_length ? left_length : right_length;
    int order = memcmp(left, right, common);
    if (order != 0) {
        return order;
    }
    return (left_length > right_length) - (left_length < right_length);
}

static const type_info_t INT4_TYPE = {TYPE_OID_INT4, "int4",      sizeof(int32_t),
                                      int4_input,    int4_output, int4_compare};
static const type_info_t TEXT_TYPE = {TYPE_OID_TEXT, "text",      TYPE_VARIABLE_LENGTH,
                                      text_input,    text_output, text_compare};

static const type_info_t *const TYPES[] = {&INT4_TYPE, &TEXT_TYPE};

typedef struct {
    const char *name;
    const type_info_t *type;
} type_alias_t;

static const type_alias_t ALIASES[] = {
    {"int4", &INT4_TYPE},
    {"int", &INT4_TYPE},
    {"integer", &INT4_TYPE},
    {"text", &TEXT_TYPE},
};

const type_info_t *type_find_by_name(const char *name)
{
    size_t count = sizeof(ALIASES) / sizeof(ALIASES[0]);
    size_t index = 0;
    while (index < count && strcasecmp(ALIASES[index].name, name) != 0) {
        index++;
    }
    errno = index == count ? ENOENT : errno;
    return index == count ? nullptr : ALIASES[index].type;
}

const type_info_t *type_find_by_oid(type_oid_t oid)
{
    size_t count = sizeof(TYPES) / sizeof(TYPES[0]);
    size_t index = 0;
    while (index < count && TYPES[index]->oid != oid) {
        index++;
    }
    errno = index == count ? ENOENT : errno;
    return index == count ? nullptr : TYPES[index];
}
