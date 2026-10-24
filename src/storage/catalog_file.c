#include "storage/catalog_file.h"

#include <errno.h>
#include <string.h>

#include "storage/one_page_engine.h"
#include "storage/one_page_file.h"
#include "storage/row_codec.h"

typedef enum {
    RECORD_VERSION = 0,
    RECORD_TABLE = 1,
    RECORD_COLUMN = 2,
} record_kind_t;

enum {
    FIELD_KIND,
    FIELD_TABLE,
    FIELD_NAME,
    FIELD_FIRST_NUMBER,
    FIELD_SECOND_NUMBER,
    FIELD_COUNT,
};

static const type_info_t *const RECORD_TYPES[FIELD_COUNT] = {&TYPE_INT4, &TYPE_TEXT, &TYPE_TEXT,
                                                             &TYPE_INT4, &TYPE_INT4};
static const row_layout_t RECORD_LAYOUT = {RECORD_TYPES, FIELD_COUNT};

typedef struct {
    int32_t kind;
    const char *table;
    const char *name;
    const int32_t *first_number;
    const int32_t *second_number;
} record_t;

static row_value_t text_value(const char *text)
{
    return (row_value_t){text, text == nullptr ? 0 : strlen(text)};
}

static row_value_t number_value(const int32_t *number)
{
    return (row_value_t){number, number == nullptr ? 0 : sizeof(*number)};
}

static int put_record(page_t *page, const record_t *record)
{
    const row_value_t values[FIELD_COUNT] = {
        number_value(&record->kind), text_value(record->table), text_value(record->name),
        number_value(record->first_number), number_value(record->second_number)};
    unsigned char row[PAGE_SIZE];
    ssize_t size = row_encode(&RECORD_LAYOUT, values, row, sizeof(row));
    if (size < 0) {
        return -1;
    }
    return page_insert(page, row, (size_t) size) < 0 ? -1 : 0;
}

static size_t display_position_of(table_t *table, size_t physical)
{
    size_t display = 0;
    while (table_physical_index(table, display) != physical) {
        display++;
    }
    return display;
}

static int put_column(page_t *page, table_t *table, size_t physical)
{
    const table_column_t *column = table_column(table, physical);
    const int32_t oid = (int32_t) column->type->oid;
    const int32_t position = (int32_t) display_position_of(table, physical);
    const record_t record = {RECORD_COLUMN, table_name(table), column->name, &oid, &position};
    return put_record(page, &record);
}

static int put_table(page_t *page, table_t *table)
{
    const record_t record = {RECORD_TABLE, table_name(table), ONE_PAGE_ENGINE_NAME, nullptr,
                             nullptr};
    int status = put_record(page, &record);
    table_lock(table);
    for (size_t column = 0; status == 0 && column < table_column_count(table); column++) {
        status = put_column(page, table, column);
    }
    table_unlock(table);
    return status;
}

int catalog_file_save(const char *path, table_t *const *tables, size_t count)
{
    page_t page;
    const int32_t version = CATALOG_FILE_FORMAT_VERSION;
    const record_t record = {RECORD_VERSION, nullptr, nullptr, &version, nullptr};

    page_init(&page, PAGE_TYPE_CATALOG);
    int status = put_record(&page, &record);
    for (size_t i = 0; status == 0 && i < count; i++) {
        status = put_table(&page, tables[i]);
    }
    return status == 0 ? one_page_file_save(path, &page) : status;
}
