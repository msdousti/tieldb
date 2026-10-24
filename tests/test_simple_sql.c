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

static void test_text_without_a_statement_is_empty([[maybe_unused]] void **state)
{
    assert_true(sql_is_empty(""));
    assert_true(sql_is_empty(";"));
    assert_true(sql_is_empty("  ; ;;\n\t; "));
    assert_false(sql_is_empty("SELECT 1"));
    assert_false(sql_is_empty(" ; x ;"));
}

static void test_checkpoint_is_recognized([[maybe_unused]] void **state)
{
    assert_true(sql_is_checkpoint("CHECKPOINT"));
    assert_true(sql_is_checkpoint("  checkpoint ;  "));
    assert_true(sql_is_checkpoint("\nCheckPoint\n"));
    assert_false(sql_is_checkpoint("checkpoints"));
    assert_false(sql_is_checkpoint("checkpoint now"));
    assert_false(sql_is_checkpoint("check"));
    assert_false(sql_is_checkpoint(""));
}

static void test_version_query_is_recognized([[maybe_unused]] void **state)
{
    assert_true(sql_is_version_query("SELECT version()"));
    assert_true(sql_is_version_query("  select VERSION() ;  "));
    assert_true(sql_is_version_query("\nselect version()\n"));
}

static void test_current_user_and_database_queries_are_recognized([[maybe_unused]] void **state)
{
    assert_true(sql_is_current_user_query("select current_user"));
    assert_true(sql_is_current_user_query(" SELECT Current_User ; "));
    assert_false(sql_is_current_user_query("select current_user()"));
    assert_false(sql_is_current_user_query("select current_database()"));
    assert_true(sql_is_current_database_query("select current_database()"));
    assert_true(sql_is_current_database_query("SELECT CURRENT_DATABASE();"));
    assert_false(sql_is_current_database_query("select current_database"));
    assert_false(sql_is_current_database_query("select current_user"));
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

static void assert_column_is(const sql_create_table_t *create, size_t index, const char *name,
                             const char *type)
{
    assert_string_equal(create->columns[index].name, name);
    assert_string_equal(create->columns[index].type, type);
}

static void assert_not_create_table(const char *sql)
{
    sql_create_table_t create;

    assert_false(sql_parse_create_table(sql, &create));
}

static void test_create_table_gives_the_table_and_the_columns([[maybe_unused]] void **state)
{
    sql_create_table_t create;

    assert_true(sql_parse_create_table("CREATE TABLE user (id INTEGER, name TEXT);", &create));
    assert_string_equal(create.table, "user");
    assert_int_equal(create.count, 2);
    assert_column_is(&create, 0, "id", "integer");
    assert_column_is(&create, 1, "name", "text");
}

static void test_create_table_ignores_case_and_spaces([[maybe_unused]] void **state)
{
    sql_create_table_t create;

    assert_true(sql_parse_create_table("  create  table  T1 ( A  Int ) ; ", &create));
    assert_string_equal(create.table, "t1");
    assert_int_equal(create.count, 1);
    assert_column_is(&create, 0, "a", "int");
}

static void test_create_table_may_have_no_columns([[maybe_unused]] void **state)
{
    sql_create_table_t create;

    assert_true(sql_parse_create_table("create table t();", &create));
    assert_string_equal(create.table, "t");
    assert_int_equal(create.count, 0);
    assert_true(sql_parse_create_table("CREATE TABLE t ( )", &create));
    assert_int_equal(create.count, 0);
}

static void test_text_that_is_not_a_create_table_is_rejected([[maybe_unused]] void **state)
{
    assert_not_create_table("");
    assert_not_create_table("CREATE");
    assert_not_create_table("CREATETABLE t (a int)");
    assert_not_create_table("CREATE t (a int)");
    assert_not_create_table("CREATE TABLE (a int)");
    assert_not_create_table("CREATE TABLE 1t (a int)");
    assert_not_create_table("CREATE TABLE t a int");
    assert_not_create_table("CREATE TABLE t (a)");
    assert_not_create_table("CREATE TABLE t (a int,)");
    assert_not_create_table("CREATE TABLE t (,)");
    assert_not_create_table("CREATE TABLE t (a int b int)");
    assert_not_create_table("CREATE TABLE t (a int");
    assert_not_create_table("CREATE TABLE t (a int) x");
}

static size_t build_create_with_columns(char *sql, size_t capacity, size_t columns)
{
    size_t used = (size_t) snprintf(sql, capacity, "CREATE TABLE t (c0 int");

    for (size_t i = 1; i < columns; i++) {
        used += (size_t) snprintf(sql + used, capacity - used, ",c%zu int", i);
    }
    used += (size_t) snprintf(sql + used, capacity - used, ")");
    return used;
}

static void test_create_table_with_too_many_columns_is_rejected([[maybe_unused]] void **state)
{
    char sql[512];
    sql_create_table_t create;

    build_create_with_columns(sql, sizeof(sql), SQL_MAX_CREATE_COLUMNS);
    assert_true(sql_parse_create_table(sql, &create));
    assert_int_equal(create.count, SQL_MAX_CREATE_COLUMNS);

    build_create_with_columns(sql, sizeof(sql), SQL_MAX_CREATE_COLUMNS + 1);
    assert_false(sql_parse_create_table(sql, &create));
}

static const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_text_without_a_statement_is_empty),
    cmocka_unit_test(test_checkpoint_is_recognized),
    cmocka_unit_test(test_version_query_is_recognized),
    cmocka_unit_test(test_current_user_and_database_queries_are_recognized),
    cmocka_unit_test(test_other_text_is_not_a_version_query),
    cmocka_unit_test(test_select_star_gives_the_lower_cased_table_name),
    cmocka_unit_test(test_statements_that_are_not_select_star_are_rejected),
    cmocka_unit_test(test_table_names_that_do_not_fit_are_rejected),
    cmocka_unit_test(test_insert_gives_the_table_and_the_literals),
    cmocka_unit_test(test_insert_ignores_case_and_spaces_and_unescapes_strings),
    cmocka_unit_test(test_text_that_is_not_an_insert_is_rejected),
    cmocka_unit_test(test_insert_with_too_many_values_is_rejected),
    cmocka_unit_test(test_create_table_gives_the_table_and_the_columns),
    cmocka_unit_test(test_create_table_ignores_case_and_spaces),
    cmocka_unit_test(test_create_table_may_have_no_columns),
    cmocka_unit_test(test_text_that_is_not_a_create_table_is_rejected),
    cmocka_unit_test(test_create_table_with_too_many_columns_is_rejected),
};

int main(void)
{
    return cmocka_run_group_tests(tests, nullptr, nullptr);
}
