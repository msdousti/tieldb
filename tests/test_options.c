/**
 * Tests for command-line option parsing
 */

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "options.h"

#define PARSE(...)                                                                    \
    options_parse((int) (sizeof((char *[]){"tieldb", __VA_ARGS__}) / sizeof(char *)), \
                  (char *[]){"tieldb", __VA_ARGS__}, &options)

static void assert_rejected(int status)
{
    assert_int_equal(status, -1);
    assert_int_equal(errno, EINVAL);
}

static void test_defaults_to_the_postgres_port_and_no_limit([[maybe_unused]] void **state)
{
    options_t options;

    char *const argv[] = {"tieldb"};

    assert_int_equal(options_parse(1, argv, &options), 0);
    assert_int_equal(options.port, 5432);
    assert_int_equal(options.max_connections, SIZE_MAX);
    assert_null(options.data_dir);
}

static void test_port_and_once_can_be_combined([[maybe_unused]] void **state)
{
    options_t options;

    assert_int_equal(PARSE("--once", "--port", "6000"), 0);
    assert_int_equal(options.port, 6000);
    assert_int_equal(options.max_connections, 1);
}

static void test_data_dir_takes_a_path([[maybe_unused]] void **state)
{
    options_t options;

    assert_int_equal(PARSE("--data-dir", "/var/tieldb", "--once"), 0);
    assert_string_equal(options.data_dir, "/var/tieldb");
    assert_int_equal(options.max_connections, 1);
    assert_rejected(PARSE("--data-dir"));
}

static void test_port_zero_and_the_maximum_port_are_accepted([[maybe_unused]] void **state)
{
    options_t options;

    assert_int_equal(PARSE("--port", "0"), 0);
    assert_int_equal(options.port, 0);
    assert_int_equal(PARSE("--port", "65535"), 0);
    assert_int_equal(options.port, UINT16_MAX);
}

static void test_unknown_option_is_rejected([[maybe_unused]] void **state)
{
    options_t options;

    assert_rejected(PARSE("--bogus"));
    assert_rejected(PARSE("--bogus", "--once"));
}

static void test_port_without_a_value_is_rejected([[maybe_unused]] void **state)
{
    options_t options;

    assert_rejected(PARSE("--port"));
}

static void test_malformed_ports_are_rejected([[maybe_unused]] void **state)
{
    options_t options;

    assert_rejected(PARSE("--port", ""));
    assert_rejected(PARSE("--port", "12x"));
    assert_rejected(PARSE("--port", "-1"));
    assert_rejected(PARSE("--port", "65536"));
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_defaults_to_the_postgres_port_and_no_limit),
    cmocka_unit_test(test_port_and_once_can_be_combined),
    cmocka_unit_test(test_data_dir_takes_a_path),
    cmocka_unit_test(test_port_zero_and_the_maximum_port_are_accepted),
    cmocka_unit_test(test_unknown_option_is_rejected),
    cmocka_unit_test(test_port_without_a_value_is_rejected),
    cmocka_unit_test(test_malformed_ports_are_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
