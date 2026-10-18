/**
 * Out-of-memory tests for the wire protocol output buffer.
 *
 * wire_buffer.c is built for this test with malloc, calloc and realloc renamed to the oom_*
 * functions below (see tests/meson.build). Each one asks cmocka whether the call succeeds, so
 * every test spells out the allocation sequence it expects, and cmocka fails the test if the
 * code makes more or fewer calls.
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "net/protocol.h"
#include "net/wire_buffer.h"

// NOLINTBEGIN(misc-use-internal-linkage): called from wire_buffer.c, a separate translation unit
void *oom_malloc(size_t size)
{
    if (mock_type(bool)) {
        return malloc(size);
    }
    errno = ENOMEM;
    return nullptr;
}

void *oom_calloc(size_t count, size_t size)
{
    if (mock_type(bool)) {
        return calloc(count, size);
    }
    errno = ENOMEM;
    return nullptr;
}

void *oom_realloc(void *ptr, size_t size)
{
    if (mock_type(bool)) {
        return realloc(ptr, size);
    }
    errno = ENOMEM;
    return nullptr;
}
// NOLINTEND(misc-use-internal-linkage)

static wire_writer_t *create_writer_with_real_allocations(void)
{
    will_return(oom_calloc, true);
    will_return(oom_malloc, true);
    wire_writer_t *writer = wire_writer_create();
    if (writer == nullptr) {
        fail();
    }
    return writer;
}

static void test_create_fails_when_struct_allocation_fails([[maybe_unused]] void **state)
{
    will_return(oom_calloc, false);
    errno = 0;

    wire_writer_t *w = wire_writer_create();

    assert_null(w);
    assert_int_equal(errno, ENOMEM);
}

static void test_create_fails_when_buffer_allocation_fails([[maybe_unused]] void **state)
{
    will_return(oom_calloc, true);
    will_return(oom_malloc, false);
    errno = 0;

    wire_writer_t *w = wire_writer_create();

    assert_null(w);
    assert_int_equal(errno, ENOMEM);
}

static void test_put_bytes_fails_when_growth_fails([[maybe_unused]] void **state)
{
    uint8_t chunk[200] = {};
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(wire_writer_put_bytes(w, chunk, sizeof(chunk)), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

static void test_reader_create_fails_when_allocation_fails([[maybe_unused]] void **state)
{
    const uint8_t input[] = {1};
    will_return(oom_calloc, false);
    errno = 0;

    wire_reader_t *r = wire_reader_create(input, sizeof(input));

    assert_null(r);
    assert_int_equal(errno, ENOMEM);
}

static void
test_command_complete_leaves_writer_unchanged_when_header_fails([[maybe_unused]] void **state)
{
    wire_writer_t *w = create_writer_with_real_allocations();
    uint8_t filler[60] = {};
    assert_int_equal(wire_writer_put_bytes(w, filler, sizeof(filler)), 0);

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_command_complete(w, "SELECT 1"), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), sizeof(filler));
    wire_writer_destroy(w);
}

static void test_command_complete_rolls_back_header_when_tag_fails([[maybe_unused]] void **state)
{
    char tag[100];
    memset(tag, 'x', sizeof(tag) - 1);
    tag[sizeof(tag) - 1] = '\0';
    wire_writer_t *w = create_writer_with_real_allocations();

    will_return(oom_realloc, false);
    errno = 0;
    assert_int_equal(protocol_put_command_complete(w, tag), -1);

    assert_int_equal(errno, ENOMEM);
    assert_int_equal(wire_writer_size(w), 0);
    wire_writer_destroy(w);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_fails_when_struct_allocation_fails),
        cmocka_unit_test(test_create_fails_when_buffer_allocation_fails),
        cmocka_unit_test(test_put_bytes_fails_when_growth_fails),
        cmocka_unit_test(test_reader_create_fails_when_allocation_fails),
        cmocka_unit_test(test_command_complete_leaves_writer_unchanged_when_header_fails),
        cmocka_unit_test(test_command_complete_rolls_back_header_when_tag_fails),
    };
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
