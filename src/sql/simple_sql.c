/**
 * Recognizers for the few SQL statements TielDB understands before it has a real parser.
 */

#include "sql/simple_sql.h"

#include <ctype.h>

static constexpr char SELECT_KEYWORD[] = "select";
static constexpr char FROM_KEYWORD[] = "from";
static constexpr char VERSION_CALL[] = "version()";
static constexpr char STAR = '*';
static constexpr char UNDERSCORE = '_';
static constexpr char SEMICOLON = ';';

static const char *skip_spaces(const char *text)
{
    while (isspace((unsigned char) *text)) {
        text++;
    }
    return text;
}

static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char) (c - 'A' + 'a');
    }
    return c;
}

static bool consume_ignoring_case(const char **cursor, const char *word)
{
    const char *text = *cursor;
    while (*word != '\0' && to_lower(*text) == *word) {
        word++;
        text++;
    }
    if (*word != '\0') {
        return false;
    }
    *cursor = text;
    return true;
}

static bool is_end_of_statement(const char *cursor)
{
    cursor = skip_spaces(cursor);
    if (*cursor == SEMICOLON) {
        cursor = skip_spaces(cursor + 1);
    }
    return *cursor == '\0';
}

static bool is_identifier_char(char c)
{
    return isalnum((unsigned char) c) || c == UNDERSCORE;
}

static bool consume_identifier(const char **cursor, char *out, size_t capacity)
{
    const char *text = *cursor;
    size_t length = 0;
    while (is_identifier_char(text[length])) {
        length++;
    }
    if (length == 0 || isdigit((unsigned char) *text) || length >= capacity) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        out[i] = to_lower(text[i]);
    }
    out[length] = '\0';
    *cursor = text + length;
    return true;
}

bool sql_is_version_query(const char *sql)
{
    const char *cursor = skip_spaces(sql);
    if (!consume_ignoring_case(&cursor, SELECT_KEYWORD)) {
        return false;
    }
    cursor = skip_spaces(cursor);
    return consume_ignoring_case(&cursor, VERSION_CALL) && is_end_of_statement(cursor);
}

bool sql_parse_select_all(const char *sql, char *table, size_t capacity)
{
    const char *cursor = skip_spaces(sql);
    if (!consume_ignoring_case(&cursor, SELECT_KEYWORD)) {
        return false;
    }
    cursor = skip_spaces(cursor);
    if (*cursor != STAR) {
        return false;
    }
    cursor = skip_spaces(cursor + 1);
    if (!consume_ignoring_case(&cursor, FROM_KEYWORD) || !isspace((unsigned char) *cursor)) {
        return false;
    }
    cursor = skip_spaces(cursor);
    return consume_identifier(&cursor, table, capacity) && is_end_of_statement(cursor);
}
