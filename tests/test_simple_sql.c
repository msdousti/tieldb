/**
 * Tests for the simple SQL recognizers
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
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
    assert_table_is("SELECT * FROM user", "user");
    assert_table_is("  select  *  from  Big_Table_2 ; ", "big_table_2");
    assert_table_is("select*from t", "t");
}

static void test_statements_that_are_not_select_star_are_rejected([[maybe_unused]] void **state)
{
    assert_not_select_all("");
    assert_not_select_all("SELECT 1");
    assert_not_select_all("SELECT id FROM user");
    assert_not_select_all("SELECT * user");
    assert_not_select_all("SELECT * FROMuser");
    assert_not_select_all("SELECT * FROM");
    assert_not_select_all("SELECT * FROM ;");
    assert_not_select_all("SELECT * FROM 1user");
    assert_not_select_all("SELECT * FROM user WHERE id = 1");
    assert_not_select_all("SELECT * FROM user;;");
}

static void test_table_names_that_do_not_fit_are_rejected([[maybe_unused]] void **state)
{
    char table[4];

    assert_true(sql_parse_select_all("SELECT * FROM abc", table, sizeof(table)));
    assert_false(sql_parse_select_all("SELECT * FROM abcd", table, sizeof(table)));
}

static void assert_value_is(const sql_insert_t *insert, size_t index, const char *text,
                            bool is_quoted)
{
    assert_string_equal(insert->values[index].text, text);
    assert_int_equal(insert->values[index].is_quoted, is_quoted);
}

static bool parse_insert(const char *sql, sql_insert_t *insert)
{
    static char scratch[256];

    return sql_parse_insert(sql, scratch, insert);
}

static void assert_not_insert(const char *sql)
{
    sql_insert_t insert;

    assert_false(parse_insert(sql, &insert));
}

static void test_insert_gives_the_table_and_the_literals([[maybe_unused]] void **state)
{
    sql_insert_t insert;

    assert_true(parse_insert("INSERT INTO user VALUES (1, 'Alice');", &insert));
    assert_string_equal(insert.table, "user");
    assert_int_equal(insert.count, 2);
    assert_value_is(&insert, 0, "1", false);
    assert_value_is(&insert, 1, "Alice", true);
}

static void test_insert_ignores_case_and_spaces_and_unescapes_strings([[maybe_unused]] void **state)
{
    sql_insert_t insert;

    assert_true(parse_insert("  insert  into  User  values ( -5 ,'O''Brien', '' ) ; ", &insert));
    assert_string_equal(insert.table, "user");
    assert_int_equal(insert.count, 3);
    assert_value_is(&insert, 0, "-5", false);
    assert_value_is(&insert, 1, "O'Brien", true);
    assert_value_is(&insert, 2, "", true);
}

static void test_text_that_is_not_an_insert_is_rejected([[maybe_unused]] void **state)
{
    assert_not_insert("");
    assert_not_insert("SELECT 1");
    assert_not_insert("INSERTINTO t VALUES (1)");
    assert_not_insert("INSERT t VALUES (1)");
    assert_not_insert("INSERT INTO VALUES (1)");
    assert_not_insert("INSERT INTO 1t VALUES (1)");
    assert_not_insert("INSERT INTO t (1)");
    assert_not_insert("INSERT INTO t VALUES 1");
    assert_not_insert("INSERT INTO t VALUES ()");
    assert_not_insert("INSERT INTO t VALUES (1,)");
    assert_not_insert("INSERT INTO t VALUES (1 2)");
    assert_not_insert("INSERT INTO t VALUES (1");
    assert_not_insert("INSERT INTO t VALUES (1) x");
    assert_not_insert("INSERT INTO t VALUES ('abc)");
    assert_not_insert("INSERT INTO t VALUES (12abc)");
    assert_not_insert("INSERT INTO t VALUES (-)");
}

static size_t build_insert_with_values(char *sql, size_t capacity, size_t values)
{
    size_t used = (size_t) snprintf(sql, capacity, "INSERT INTO t VALUES (1");

    for (size_t i = 1; i < values; i++) {
        used += (size_t) snprintf(sql + used, capacity - used, ",1");
    }
    used += (size_t) snprintf(sql + used, capacity - used, ")");
    return used;
}

static void test_insert_with_too_many_values_is_rejected([[maybe_unused]] void **state)
{
    char sql[256];
    sql_insert_t insert;

    build_insert_with_values(sql, sizeof(sql), SQL_MAX_INSERT_VALUES);
    assert_true(parse_insert(sql, &insert));
    assert_int_equal(insert.count, SQL_MAX_INSERT_VALUES);

    build_insert_with_values(sql, sizeof(sql), SQL_MAX_INSERT_VALUES + 1);
    assert_false(parse_insert(sql, &insert));
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_version_query_is_recognized),
    cmocka_unit_test(test_other_text_is_not_a_version_query),
    cmocka_unit_test(test_select_star_gives_the_lower_cased_table_name),
    cmocka_unit_test(test_statements_that_are_not_select_star_are_rejected),
    cmocka_unit_test(test_table_names_that_do_not_fit_are_rejected),
    cmocka_unit_test(test_insert_gives_the_table_and_the_literals),
    cmocka_unit_test(test_insert_ignores_case_and_spaces_and_unescapes_strings),
    cmocka_unit_test(test_text_that_is_not_an_insert_is_rejected),
    cmocka_unit_test(test_insert_with_too_many_values_is_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
