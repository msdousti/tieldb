#include "storage/heap_tuple.h"

#include <errno.h>
#include <string.h>

static constexpr size_t XIDS_SIZE = 2 * sizeof(uint64_t);

static bool has_reserved_flags(uint16_t flags)
{
    return (flags & ~HEAP_TUPLE_HAS_XIDS) != 0;
}

static size_t header_size(uint16_t flags)
{
    bool has_xids = (flags & HEAP_TUPLE_HAS_XIDS) != 0;
    return sizeof(uint16_t) + (has_xids ? XIDS_SIZE : 0);
}

ssize_t heap_tuple_size(const heap_tuple_t *tuple)
{
    if (has_reserved_flags(tuple->flags)) {
        errno = EINVAL;
        return -1;
    }
    return (ssize_t) (header_size(tuple->flags) + tuple->row_length);
}

static void write_header(unsigned char *bytes, const heap_tuple_t *tuple)
{
    memcpy(bytes, &tuple->flags, sizeof(tuple->flags));
    if ((tuple->flags & HEAP_TUPLE_HAS_XIDS) != 0) {
        memcpy(bytes + sizeof(tuple->flags), &tuple->xmin, sizeof(tuple->xmin));
        memcpy(bytes + sizeof(tuple->flags) + sizeof(tuple->xmin), &tuple->xmax,
               sizeof(tuple->xmax));
    }
}

static void read_xids(const unsigned char *bytes, heap_tuple_t *tuple)
{
    memcpy(&tuple->xmin, bytes + sizeof(tuple->flags), sizeof(tuple->xmin));
    memcpy(&tuple->xmax, bytes + sizeof(tuple->flags) + sizeof(tuple->xmin), sizeof(tuple->xmax));
}

ssize_t heap_tuple_encode(const heap_tuple_t *tuple, void *buffer, size_t capacity)
{
    ssize_t size = heap_tuple_size(tuple);
    if (size < 0) {
        return -1;
    }
    if ((size_t) size > capacity) {
        errno = E2BIG;
        return -1;
    }
    unsigned char *bytes = buffer;
    write_header(bytes, tuple);
    if (tuple->row_length > 0) { // the row of an empty tuple may be nullptr
        memcpy(bytes + header_size(tuple->flags), tuple->row, tuple->row_length);
    }
    return size;
}

int heap_tuple_decode(const void *bytes, size_t length, heap_tuple_t *tuple)
{
    const unsigned char *input = bytes;
    *tuple = (heap_tuple_t){};
    if (length < sizeof(tuple->flags)) {
        errno = EBADMSG;
        return -1;
    }
    memcpy(&tuple->flags, input, sizeof(tuple->flags));
    if (has_reserved_flags(tuple->flags) || length < header_size(tuple->flags)) {
        errno = EBADMSG;
        return -1;
    }
    if ((tuple->flags & HEAP_TUPLE_HAS_XIDS) != 0) {
        read_xids(input, tuple);
    }
    tuple->row = input + header_size(tuple->flags);
    tuple->row_length = length - header_size(tuple->flags);
    return 0;
}
