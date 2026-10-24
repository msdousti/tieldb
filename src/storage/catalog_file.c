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

typedef struct {
    char name[CATALOG_FILE_NAME_CAPACITY];
    char engine[CATALOG_FILE_NAME_CAPACITY];
    char column_names[TABLE_MAX_COLUMNS][CATALOG_FILE_NAME_CAPACITY];
    table_column_t columns[TABLE_MAX_COLUMNS];
    size_t display_position[TABLE_MAX_COLUMNS];
    size_t column_count;
    bool is_open;
} pending_table_t;

typedef struct {
    catalog_file_visitor_t visit;
    void *context;
    pending_table_t pending;
} loader_t;

static int malformed(void)
{
    errno = EBADMSG;
    return -1;
}

static int copy_name(char *destination, const row_value_t *value)
{
    if (value->data == nullptr || value->length >= CATALOG_FILE_NAME_CAPACITY) {
        return malformed();
    }
    memcpy(destination, value->data, value->length);
    destination[value->length] = '\0';
    return 0;
}

static int read_number(int32_t *number, const row_value_t *value)
{
    if (value->data == nullptr) {
        return malformed();
    }
    memcpy(number, value->data, sizeof(*number));
    return 0;
}

static int flush_table(loader_t *loader)
{
    const pending_table_t *pending = &loader->pending;
    const catalog_file_table_t table = {pending->name, pending->engine, pending->column_count,
                                        pending->columns, pending->display_position};
    if (!pending->is_open) {
        return 0;
    }
    loader->pending.is_open = false;
    return loader->visit(&table, loader->context);
}

static int start_table(loader_t *loader, const row_value_t *fields)
{
    if (flush_table(loader) != 0) {
        return -1;
    }
    pending_table_t *pending = &loader->pending;
    pending->column_count = 0;
    pending->is_open = true;
    if (copy_name(pending->name, &fields[FIELD_TABLE]) != 0) {
        return -1;
    }
    return copy_name(pending->engine, &fields[FIELD_NAME]);
}

static int read_column_type(const row_value_t *value, const type_info_t **type)
{
    int32_t oid = 0;
    if (read_number(&oid, value) != 0) {
        return -1;
    }
    *type = type_find_by_oid((type_oid_t) oid);
    return *type == nullptr ? malformed() : 0;
}

static bool is_column_of_pending_table(const pending_table_t *pending, const row_value_t *table)
{
    return pending->is_open && pending->column_count < TABLE_MAX_COLUMNS &&
           table->data != nullptr && table->length == strlen(pending->name) &&
           memcmp(table->data, pending->name, table->length) == 0;
}

static int add_column(loader_t *loader, const row_value_t *fields)
{
    pending_table_t *pending = &loader->pending;
    size_t index = pending->column_count;
    int32_t position = 0;
    if (!is_column_of_pending_table(pending, &fields[FIELD_TABLE])) {
        return malformed();
    }
    if (copy_name(pending->column_names[index], &fields[FIELD_NAME]) != 0 ||
        read_column_type(&fields[FIELD_FIRST_NUMBER], &pending->columns[index].type) != 0 ||
        read_number(&position, &fields[FIELD_SECOND_NUMBER]) != 0) {
        return -1;
    }
    pending->columns[index].name = pending->column_names[index];
    pending->display_position[index] = (size_t) position;
    pending->column_count++;
    return 0;
}

static int handle_record(loader_t *loader, const row_value_t *fields)
{
    int32_t kind = -1;
    if (read_number(&kind, &fields[FIELD_KIND]) != 0) {
        return -1;
    }
    if (kind == RECORD_TABLE) {
        return start_table(loader, fields);
    }
    return kind == RECORD_COLUMN ? add_column(loader, fields) : malformed();
}

static int check_version(const row_value_t *fields)
{
    int32_t kind = -1;
    int32_t version = 0;
    if (read_number(&kind, &fields[FIELD_KIND]) != 0 ||
        read_number(&version, &fields[FIELD_FIRST_NUMBER]) != 0) {
        return -1;
    }
    return kind == RECORD_VERSION && version == CATALOG_FILE_FORMAT_VERSION ? 0 : malformed();
}

static int load_record(loader_t *loader, const page_t *page, size_t slot)
{
    row_value_t fields[FIELD_COUNT];
    size_t length = 0;
    const void *row = page_get(page, slot, &length);
    if (row == nullptr || row_decode(&RECORD_LAYOUT, row, length, fields) != 0) {
        return malformed();
    }
    return slot == 0 ? check_version(fields) : handle_record(loader, fields);
}

static int check_page(const page_t *page)
{
    return page_type(page) == PAGE_TYPE_CATALOG && page_slot_count(page) > 0 ? 0 : malformed();
}

int catalog_file_load(const char *path, catalog_file_visitor_t visit, void *context)
{
    page_t page;
    loader_t loader = {.visit = visit, .context = context};
    int status = one_page_file_load(path, &page);
    status = status == 0 ? check_page(&page) : status;
    for (size_t slot = 0; status == 0 && slot < page_slot_count(&page); slot++) {
        status = load_record(&loader, &page, slot);
    }
    return status == 0 ? flush_table(&loader) : status;
}
