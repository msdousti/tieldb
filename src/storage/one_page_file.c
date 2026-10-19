#include "storage/one_page_file.h"

#include <errno.h>

#include "storage/page_file.h"

static constexpr size_t THE_PAGE = 0;

static int finish(page_file_t *file, int status)
{
    int closed = page_file_close(file);
    return status != 0 ? status : closed;
}

static page_file_t *open_or_create(const char *path)
{
    page_file_t *file = page_file_open(path);
    return file == nullptr && errno == ENOENT ? page_file_create(path) : file;
}

int one_page_file_save(const char *path, const page_t *page)
{
    page_file_t *file = open_or_create(path);
    if (file == nullptr) {
        return -1;
    }
    int status = page_file_write(file, THE_PAGE, page);
    return finish(file, status == 0 ? page_file_sync(file) : status);
}

int one_page_file_load(const char *path, page_t *page)
{
    page_file_t *file = page_file_open(path);
    if (file == nullptr) {
        return -1;
    }
    return finish(file, page_file_read(file, THE_PAGE, page));
}
