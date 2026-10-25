/**
 * Removing the directory that a test made, with the files in it.
 */

#pragma once

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

// A new empty directory; name_template ends in XXXXXX, as for mkdtemp, and becomes its name
static inline void make_directory(char *name_template)
{
    if (mkdtemp(name_template) == nullptr) {
        abort();
    }
}

// The tests run one thread, so readdir is fine
// NOLINTBEGIN(concurrency-mt-unsafe)
static inline void remove_directory(const char *root)
{
    char path[512];
    DIR *handle = opendir(root);
    for (struct dirent *entry = handle == nullptr ? nullptr : readdir(handle); entry != nullptr;
         entry = readdir(handle)) {
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        unlink(path);
    }
    if (handle != nullptr) {
        closedir(handle);
    }
    rmdir(root);
}
// NOLINTEND(concurrency-mt-unsafe)
