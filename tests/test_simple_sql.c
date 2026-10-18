/**
 * Tests for the simple SQL recognizers
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "sql/simple_sql.h"

static void assert_table_is(const char *sql, const char *expected)
{
    char table[SQL_IDENTIFIER_CAPACITY];

    assert_true(sql_parse_select_all(sql, table, sizeof(table)));
    assert_string_equal(table, expected);
}

static void assert_not_select_all(const char *sql)
{
    char table[SQL_IDENTIFIER_CAPACITY];

    assert_false(sql_parse_select_all(sql, table, sizeof(table)));
}

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

static void test_select_star_gives_the_lower_cased_table_name([[maybe_unused]] void **state)
{
    assert_table_is("SELECT * FROM users", "users");
    assert_table_is("  select  *  from  Big_Table_2 ; ", "big_table_2");
    assert_table_is("select*from t", "t");
}

static void test_statements_that_are_not_select_star_are_rejected([[maybe_unused]] void **state)
{
    assert_not_select_all("");
    assert_not_select_all("SELECT 1");
    assert_not_select_all("SELECT id FROM users");
    assert_not_select_all("SELECT * users");
    assert_not_select_all("SELECT * FROMusers");
    assert_not_select_all("SELECT * FROM");
    assert_not_select_all("SELECT * FROM ;");
    assert_not_select_all("SELECT * FROM 1users");
    assert_not_select_all("SELECT * FROM users WHERE id = 1");
    assert_not_select_all("SELECT * FROM users;;");
}

static void test_table_names_that_do_not_fit_are_rejected([[maybe_unused]] void **state)
{
    char table[4];

    assert_true(sql_parse_select_all("SELECT * FROM abc", table, sizeof(table)));
    assert_false(sql_parse_select_all("SELECT * FROM abcd", table, sizeof(table)));
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_version_query_is_recognized),
    cmocka_unit_test(test_other_text_is_not_a_version_query),
    cmocka_unit_test(test_select_star_gives_the_lower_cased_table_name),
    cmocka_unit_test(test_statements_that_are_not_select_star_are_rejected),
    cmocka_unit_test(test_table_names_that_do_not_fit_are_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
