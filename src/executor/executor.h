/**
 * Execution of parsed SQL statements against the tables.
 *
 * executor_insert runs an INSERT. It separates two kinds of failure:
 *
 *   - the statement is wrong (unknown table, wrong number of values, text in an integer column):
 *     the call returns 0 and fills in the error with a SQLSTATE and message for the client;
 *   - the server fails (memory runs out): the call returns -1 and sets errno to ENOMEM.
 *
 * On success the error's sqlstate is empty. Integer values are checked to be valid 32-bit
 * integers and are stored as written.
 *
 * executor_create_table runs CREATE TABLE and fails the same two ways. A wrong statement is an
 * unknown column type (INTEGER, INT, INT4 and TEXT exist), a column named twice, or a table that
 * already exists. Names are as the parser produced them, in lower case.
 *
 * executor_select_all runs SELECT * FROM <table>. It cannot fail except by a wrong statement
 * (unknown table), reported in the result's error. On success the result gives the table to read
 * from, locked so the rows cannot change; the caller must call executor_select_release when it
 * is done reading, and must not insert into that table before then.
 */

#pragma once

#include "sql/simple_sql.h"
#include "storage/table.h"

constexpr size_t EXECUTOR_SQLSTATE_CAPACITY = 6;
constexpr size_t EXECUTOR_MESSAGE_CAPACITY = 128;

typedef struct {
    char sqlstate[EXECUTOR_SQLSTATE_CAPACITY];
    char message[EXECUTOR_MESSAGE_CAPACITY];
} executor_error_t;

[[nodiscard]] int executor_insert(const sql_insert_t *insert, executor_error_t *error);

[[nodiscard]] int executor_create_table(const sql_create_table_t *create, executor_error_t *error);

typedef struct {
    executor_error_t error;
    table_t *table; // nullptr when the statement failed; otherwise locked until released
} executor_select_t;

void executor_select_all(const char *table_name, executor_select_t *select);
void executor_select_release(executor_select_t *select);
