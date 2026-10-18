/**
 * Recognizers for the few SQL statements TielDB understands before it has a real parser.
 */

#include "sql/simple_sql.h"

#include <ctype.h>

static constexpr char SELECT_KEYWORD[] = "select";
static constexpr char VERSION_CALL[] = "version()";
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

bool sql_is_version_query(const char *sql)
{
    const char *cursor = skip_spaces(sql);
    if (!consume_ignoring_case(&cursor, SELECT_KEYWORD)) {
        return false;
    }
    cursor = skip_spaces(cursor);
    return consume_ignoring_case(&cursor, VERSION_CALL) && is_end_of_statement(cursor);
}
