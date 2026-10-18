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
 */

#pragma once

#include "sql/simple_sql.h"

constexpr size_t EXECUTOR_SQLSTATE_CAPACITY = 6;
constexpr size_t EXECUTOR_MESSAGE_CAPACITY = 128;

typedef struct {
    char sqlstate[EXECUTOR_SQLSTATE_CAPACITY];
    char message[EXECUTOR_MESSAGE_CAPACITY];
} executor_error_t;

[[nodiscard]] int executor_insert(const sql_insert_t *insert, executor_error_t *error);
