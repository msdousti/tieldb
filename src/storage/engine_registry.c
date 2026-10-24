#include "storage/engine_registry.h"

#include <errno.h>
#include <string.h>

#include "storage/one_page_engine.h"

static const storage_engine_factory_t *const ENGINES[] = {&ONE_PAGE_ENGINE};

const storage_engine_factory_t *engine_find(const char *name)
{
    size_t count = sizeof(ENGINES) / sizeof(ENGINES[0]);
    size_t index = 0;
    while (index < count && strcmp(ENGINES[index]->name, name) != 0) {
        index++;
    }
    if (index == count) {
        errno = ENOENT;
        return nullptr;
    }
    return ENGINES[index];
}

const storage_engine_factory_t *engine_default(void)
{
    return &ONE_PAGE_ENGINE;
}
