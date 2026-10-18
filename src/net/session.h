/**
 * What a connection knows about its client after startup.
 *
 * The user and database names come from the StartupMessage. TielDB has one database and no
 * authentication, so they are only recorded and reported back, for example by current_user.
 * Names longer than SESSION_NAME_CAPACITY - 1 characters are cut, as Postgres cuts identifiers.
 */

#pragma once

#include <stddef.h>

constexpr size_t SESSION_NAME_CAPACITY = 64;

typedef struct {
    char user[SESSION_NAME_CAPACITY];
    char database[SESSION_NAME_CAPACITY];
} session_info_t;
