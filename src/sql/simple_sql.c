/**
 * Recognizers for the few SQL statements TielDB understands before it has a real parser.
 */

#include "sql/simple_sql.h"

#include <ctype.h>
#include <string.h>

static constexpr char SELECT_KEYWORD[] = "select";
static constexpr char FROM_KEYWORD[] = "from";
static constexpr char VERSION_CALL[] = "version()";
static constexpr char STAR = '*';
static constexpr char UNDERSCORE = '_';
static constexpr char SEMICOLON = ';';
static constexpr char INSERT_KEYWORD[] = "insert";
static constexpr char INTO_KEYWORD[] = "into";
static constexpr char VALUES_KEYWORD[] = "values";
static constexpr char CREATE_KEYWORD[] = "create";
static constexpr char TABLE_KEYWORD[] = "table";
static constexpr char QUOTE = '\'';
static constexpr char MINUS = '-';

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

static bool consume_word(const char **cursor, const char *word)
{
    const char *text = skip_spaces(*cursor);
    if (!consume_ignoring_case(&text, word) || is_identifier_char(*text)) {
        return false;
    }
    *cursor = text;
    return true;
}

static bool consume_symbol(const char **cursor, char symbol)
{
    const char *text = skip_spaces(*cursor);
    if (*text != symbol) {
        return false;
    }
    *cursor = text + 1;
    return true;
}

static bool consume_name(const char **cursor, char *out, size_t capacity)
{
    const char *text = skip_spaces(*cursor);
    if (!consume_identifier(&text, out, capacity)) {
        return false;
    }
    *cursor = text;
    return true;
}

static bool consume_integer_literal(const char **cursor, char **write, sql_literal_t *literal)
{
    const char *text = *cursor;
    size_t length = *text == MINUS ? 1 : 0;
    size_t digits_start = length;
    while (isdigit((unsigned char) text[length])) {
        length++;
    }
    if (length == digits_start || is_identifier_char(text[length])) {
        return false;
    }
    memcpy(*write, text, length);
    (*write)[length] = '\0';
    *literal = (sql_literal_t){.text = *write, .is_quoted = false};
    *write += length + 1;
    *cursor = text + length;
    return true;
}

static bool consume_string_literal(const char **cursor, char **write, sql_literal_t *literal)
{
    const char *text = *cursor + 1;
    char *out = *write;
    while (*text != '\0' && (*text != QUOTE || text[1] == QUOTE)) {
        text += (*text == QUOTE) ? 1 : 0;
        *out++ = *text++;
    }
    if (*text == '\0') {
        return false;
    }
    *out = '\0';
    *literal = (sql_literal_t){.text = *write, .is_quoted = true};
    *write = out + 1;
    *cursor = text + 1;
    return true;
}

static bool consume_literal(const char **cursor, char **write, sql_literal_t *literal)
{
    *cursor = skip_spaces(*cursor);
    if (**cursor == QUOTE) {
        return consume_string_literal(cursor, write, literal);
    }
    return consume_integer_literal(cursor, write, literal);
}

static bool consume_next_literal(const char **cursor, char **write, sql_insert_t *insert)
{
    if (insert->count == SQL_MAX_INSERT_VALUES) {
        return false;
    }
    return consume_literal(cursor, write, &insert->values[insert->count++]);
}

static bool consume_value_list(const char **cursor, char **write, sql_insert_t *insert)
{
    bool is_valid = true;
    bool has_more = true;
    while (is_valid && has_more) {
        is_valid = consume_next_literal(cursor, write, insert);
        has_more = is_valid && consume_symbol(cursor, ',');
    }
    return is_valid && consume_symbol(cursor, ')');
}

bool sql_parse_insert(const char *sql, char *scratch, sql_insert_t *insert)
{
    const char *cursor = sql;
    char *write = scratch;
    *insert = (sql_insert_t){};
    return consume_word(&cursor, INSERT_KEYWORD) && consume_word(&cursor, INTO_KEYWORD) &&
           consume_name(&cursor, insert->table, sizeof(insert->table)) &&
           consume_word(&cursor, VALUES_KEYWORD) && consume_symbol(&cursor, '(') &&
           consume_value_list(&cursor, &write, insert) && is_end_of_statement(cursor);
}

static bool consume_column_def(const char **cursor, sql_create_table_t *create)
{
    if (create->count == SQL_MAX_CREATE_COLUMNS) {
        return false;
    }
    sql_column_def_t *column = &create->columns[create->count++];
    return consume_name(cursor, column->name, sizeof(column->name)) &&
           consume_name(cursor, column->type, sizeof(column->type));
}

static bool consume_column_list(const char **cursor, sql_create_table_t *create)
{
    bool is_valid = true;
    bool has_more = true;
    while (is_valid && has_more) {
        is_valid = consume_column_def(cursor, create);
        has_more = is_valid && consume_symbol(cursor, ',');
    }
    return is_valid && consume_symbol(cursor, ')');
}

bool sql_parse_create_table(const char *sql, sql_create_table_t *create)
{
    const char *cursor = sql;
    *create = (sql_create_table_t){};
    return consume_word(&cursor, CREATE_KEYWORD) && consume_word(&cursor, TABLE_KEYWORD) &&
           consume_name(&cursor, create->table, sizeof(create->table)) &&
           consume_symbol(&cursor, '(') && consume_column_list(&cursor, create) &&
           is_end_of_statement(cursor);
}
