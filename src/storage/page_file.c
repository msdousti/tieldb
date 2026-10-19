#define _POSIX_C_SOURCE 200809L

#include "storage/page_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>

#include "io/io.h"

static constexpr mode_t FILE_MODE = 0644;

struct page_file {
    int fd;
};

page_file_t *page_file_open(const char *path)
{
    page_file_t *file = malloc(sizeof(*file));
    if (file == nullptr) {
        return nullptr;
    }
    file->fd = io->open(path, O_RDWR | O_CREAT, FILE_MODE);
    if (file->fd < 0) {
        free(file);
        return nullptr;
    }
    return file;
}

int page_file_close(page_file_t *file)
{
    int status = io->close(file->fd);
    free(file);
    return status;
}

static off_t page_offset(size_t page_number)
{
    return (off_t) (page_number * PAGE_SIZE);
}

int page_file_read(page_file_t *file, size_t page_number, page_t *page)
{
    ssize_t count = io->pread(file->fd, page->bytes, PAGE_SIZE, page_offset(page_number));
    if (count < 0) {
        return -1;
    }
    if (count == 0) {
        errno = ENOENT;
        return -1;
    }
    if ((size_t) count != PAGE_SIZE) {
        errno = EBADMSG;
        return -1;
    }
    return page_verify_checksum(page) == 0 ? page_validate(page) : -1;
}

int page_file_write(page_file_t *file, size_t page_number, const page_t *page)
{
    page_t copy = *page;
    page_set_checksum(&copy);
    ssize_t count = io->pwrite(file->fd, copy.bytes, PAGE_SIZE, page_offset(page_number));
    if (count < 0) {
        return -1;
    }
    if ((size_t) count != PAGE_SIZE) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int page_file_sync(page_file_t *file)
{
    return io->fsync(file->fd);
}
