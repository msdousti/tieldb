/**
 * Tests for the type registry
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "types/type_registry.h"

static const type_info_t *find_or_fail(const char *name)
{
    const type_info_t *type = type_find_by_name(name);
    if (type == nullptr) {
        fail();
    }
    return type;
}

static void assert_input_fails(const type_info_t *type, const char *text, int error)
{
    char buffer[16];
    errno = 0;
    assert_int_equal(type->input(text, buffer, sizeof(buffer)), -1);
    assert_int_equal(errno, error);
}

static void assert_round_trip(const type_info_t *type, const char *text, const char *expected)
{
    char stored[16];
    char shown[16];
    ssize_t length = type->input(text, stored, sizeof(stored));
    assert_true(length >= 0);
    assert_int_equal(type->output(stored, (size_t) length, shown, sizeof(shown)),
                     (ssize_t) strlen(expected));
    assert_string_equal(shown, expected);
}

static void test_names_and_aliases_find_the_same_type([[maybe_unused]] void **state)
{
    assert_ptr_equal(find_or_fail("integer"), find_or_fail("int"));
    assert_ptr_equal(find_or_fail("int4"), find_or_fail("INTEGER"));
    assert_int_equal(find_or_fail("Int")->oid, TYPE_OID_INT4);
    assert_int_equal(find_or_fail("text")->oid, TYPE_OID_TEXT);
}

static void test_unknown_name_is_not_found([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(type_find_by_name("blob"));
    assert_int_equal(errno, ENOENT);
}

static void test_types_are_found_by_oid([[maybe_unused]] void **state)
{
    assert_ptr_equal(type_find_by_oid(TYPE_OID_INT4), find_or_fail("int4"));
    assert_ptr_equal(type_find_by_oid(TYPE_OID_TEXT), find_or_fail("text"));
}

static void test_unknown_oid_is_not_found([[maybe_unused]] void **state)
{
    errno = 0;
    assert_null(type_find_by_oid(1));
    assert_int_equal(errno, ENOENT);
}

static void test_storage_sizes([[maybe_unused]] void **state)
{
    assert_int_equal(find_or_fail("int")->storage_size, sizeof(int32_t));
    assert_int_equal(find_or_fail("text")->storage_size, TYPE_VARIABLE_LENGTH);
}

static void test_integers_are_stored_as_values([[maybe_unused]] void **state)
{
    const type_info_t *type = find_or_fail("int");
    assert_round_trip(type, "007", "7");
    assert_round_trip(type, "-42", "-42");
    assert_round_trip(type, "2147483647", "2147483647");
    assert_round_trip(type, "-2147483648", "-2147483648");
}

static void test_invalid_integers_are_rejected([[maybe_unused]] void **state)
{
    const type_info_t *type = find_or_fail("int");
    assert_input_fails(type, "", EINVAL);
    assert_input_fails(type, "12x", EINVAL);
    assert_input_fails(type, "99999999999999999999", EINVAL);
    assert_input_fails(type, "-2147483649", EINVAL);
    assert_input_fails(type, "2147483648", EINVAL);
}

static void test_integer_needs_room_for_four_bytes([[maybe_unused]] void **state)
{
    char buffer[sizeof(int32_t)];
    const type_info_t *type = find_or_fail("int");
    errno = 0;
    assert_int_equal(type->input("1", buffer, sizeof(buffer) - 1), -1);
    assert_int_equal(errno, E2BIG);
    assert_int_equal(type->input("1", buffer, sizeof(buffer)), sizeof(int32_t));
}

static void test_integer_output_needs_room_for_its_digits([[maybe_unused]] void **state)
{
    char buffer[8];
    int32_t value = 12345;
    const type_info_t *type = find_or_fail("int");
    errno = 0;
    assert_int_equal(type->output(&value, sizeof(value), buffer, 5), -1);
    assert_int_equal(errno, E2BIG);
    assert_int_equal(type->output(&value, sizeof(value), buffer, 6), 5);
}

static void test_integers_compare_by_value([[maybe_unused]] void **state)
{
    int32_t small = -5;
    int32_t large = 3;
    const type_info_t *type = find_or_fail("int");
    assert_true(type->compare(&small, sizeof(small), &large, sizeof(large)) < 0);
    assert_true(type->compare(&large, sizeof(large), &small, sizeof(small)) > 0);
    assert_int_equal(type->compare(&large, sizeof(large), &large, sizeof(large)), 0);
}

static void test_text_is_stored_as_written([[maybe_unused]] void **state)
{
    const type_info_t *type = find_or_fail("text");
    assert_round_trip(type, "", "");
    assert_round_trip(type, "007", "007");
    assert_round_trip(type, "Rex", "Rex");
}

static void test_text_must_fit_the_buffers([[maybe_unused]] void **state)
{
    char buffer[4];
    const type_info_t *type = find_or_fail("text");
    errno = 0;
    assert_int_equal(type->input("abcde", buffer, sizeof(buffer)), -1);
    assert_int_equal(errno, E2BIG);
    assert_int_equal(type->input("abcd", buffer, sizeof(buffer)), 4);
    errno = 0;
    assert_int_equal(type->output("abcd", 4, buffer, sizeof(buffer)), -1);
    assert_int_equal(errno, E2BIG);
    assert_int_equal(type->output("abc", 3, buffer, sizeof(buffer)), 3);
}

static void test_text_compares_bytes_then_length([[maybe_unused]] void **state)
{
    const type_info_t *type = find_or_fail("text");
    assert_true(type->compare("ab", 2, "ac", 2) < 0);
    assert_true(type->compare("b", 1, "ab", 2) > 0);
    assert_true(type->compare("ab", 2, "abc", 3) < 0);
    assert_true(type->compare("abc", 3, "ab", 2) > 0);
    assert_int_equal(type->compare("ab", 2, "ab", 2), 0);
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_names_and_aliases_find_the_same_type),
    cmocka_unit_test(test_unknown_name_is_not_found),
    cmocka_unit_test(test_types_are_found_by_oid),
    cmocka_unit_test(test_unknown_oid_is_not_found),
    cmocka_unit_test(test_storage_sizes),
    cmocka_unit_test(test_integers_are_stored_as_values),
    cmocka_unit_test(test_invalid_integers_are_rejected),
    cmocka_unit_test(test_integer_needs_room_for_four_bytes),
    cmocka_unit_test(test_integer_output_needs_room_for_its_digits),
    cmocka_unit_test(test_integers_compare_by_value),
    cmocka_unit_test(test_text_is_stored_as_written),
    cmocka_unit_test(test_text_must_fit_the_buffers),
    cmocka_unit_test(test_text_compares_bytes_then_length),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
