/**
 * Tests for the simple SQL recognizers
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "sql/simple_sql.h"

static void test_version_query_is_recognized([[maybe_unused]] void **state)
{
    assert_true(sql_is_version_query("SELECT version()"));
    assert_true(sql_is_version_query("  select VERSION() ;  "));
    assert_true(sql_is_version_query("\nselect version()\n"));
}

static void test_other_text_is_not_a_version_query([[maybe_unused]] void **state)
{
    assert_false(sql_is_version_query(""));
    assert_false(sql_is_version_query("SELECT 1"));
    assert_false(sql_is_version_query("SELECT version"));
    assert_false(sql_is_version_query("SELECT version() FROM t"));
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_version_query_is_recognized),
    cmocka_unit_test(test_other_text_is_not_a_version_query),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
