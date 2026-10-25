#include "storage/heap_tuple.h"

#include <errno.h>
#include <string.h>

// Sizes and offsets in the encoded tuple
static constexpr size_t FLAGS_SIZE = sizeof((heap_tuple_t){}.flags);
static constexpr size_t XMIN_SIZE = sizeof((heap_tuple_t){}.xmin);
static constexpr size_t XMAX_SIZE = sizeof((heap_tuple_t){}.xmax);
static constexpr size_t XMIN_OFFSET = FLAGS_SIZE;
static constexpr size_t XMAX_OFFSET = XMIN_OFFSET + XMIN_SIZE;
static constexpr size_t XIDS_SIZE = XMIN_SIZE + XMAX_SIZE;

static bool has_xids(uint16_t flags)
{
    return (flags & HEAP_TUPLE_HAS_XIDS) != 0;
}

static bool has_reserved_flags(uint16_t flags)
{
    return (flags & ~HEAP_TUPLE_HAS_XIDS) != 0;
}

static size_t encoded_header_size(uint16_t flags)
{
    return FLAGS_SIZE + (has_xids(flags) ? XIDS_SIZE : 0);
}

ssize_t heap_tuple_encoded_size(const heap_tuple_t *tuple)
{
    if (has_reserved_flags(tuple->flags)) {
        errno = EINVAL;
        return -1;
    }
    return (ssize_t) (encoded_header_size(tuple->flags) + tuple->row.length);
}

static void write_header(unsigned char *bytes, const heap_tuple_t *tuple)
{
    memcpy(bytes, &tuple->flags, FLAGS_SIZE);
    if (has_xids(tuple->flags)) {
        memcpy(bytes + XMIN_OFFSET, &tuple->xmin, XMIN_SIZE);
        memcpy(bytes + XMAX_OFFSET, &tuple->xmax, XMAX_SIZE);
    }
}

static void read_xids(const unsigned char *bytes, heap_tuple_t *tuple)
{
    memcpy(&tuple->xmin, bytes + XMIN_OFFSET, XMIN_SIZE);
    memcpy(&tuple->xmax, bytes + XMAX_OFFSET, XMAX_SIZE);
}

ssize_t heap_tuple_encode(const heap_tuple_t *tuple, void *buffer, size_t capacity)
{
    ssize_t size = heap_tuple_encoded_size(tuple);
    if (size < 0) {
        return -1;
    }
    if ((size_t) size > capacity) {
        errno = E2BIG;
        return -1;
    }
    unsigned char *bytes = buffer;
    write_header(bytes, tuple);
    if (tuple->row.length > 0) { // the span of an empty row may have a null pointer
        memcpy(bytes + encoded_header_size(tuple->flags), tuple->row.data, tuple->row.length);
    }
    return size;
}

int heap_tuple_decode(const void *bytes, size_t length, heap_tuple_t *tuple)
{
    const unsigned char *input = bytes;
    *tuple = (heap_tuple_t){};
    if (length < FLAGS_SIZE) {
        errno = EBADMSG;
        return -1;
    }
    memcpy(&tuple->flags, input, FLAGS_SIZE);
    if (has_reserved_flags(tuple->flags) || length < encoded_header_size(tuple->flags)) {
        errno = EBADMSG;
        return -1;
    }
    if (has_xids(tuple->flags)) {
        read_xids(input, tuple);
    }
    tuple->row.data = input + encoded_header_size(tuple->flags);
    tuple->row.length = length - encoded_header_size(tuple->flags);
    return 0;
}
