/**
 * Whether the tests can use direct I/O here.
 */

#pragma once

#include <stdlib.h>
#include <unistd.h>

#include "storage/page_file.h"

// Some file systems refuse O_DIRECT (tmpfs did, for a long time); the tests that need it are
// skipped there, because page_file_open_direct fails loudly instead of giving a buffered file
static inline bool direct_io_is_available(void)
{
    char path[] = "/tmp/tieldb_direct_probe_XXXXXX";
    int fd = mkstemp(path);

    if (fd < 0) {
        return false;
    }
    close(fd);
    page_file_t *file = page_file_open_direct(path);
    bool is_available = file != nullptr;
    if (is_available) {
        (void) page_file_close(file);
    }
    unlink(path);
    return is_available;
}
