#include "storage/fsm_page.h"

#include <errno.h>

#include "storage/page_layout.h"

void fsm_page_init(fsm_page_t *fsm, page_t *page)
{
    page_init(page, PAGE_TYPE_FSM);
    fsm->page = page;
}

int fsm_page_open(fsm_page_t *fsm, page_t *page)
{
    if (page_type(page) != PAGE_TYPE_FSM) {
        errno = EINVAL;
        return -1;
    }
    fsm->page = page;
    return 0;
}

unsigned char *fsm_page_body(const fsm_page_t *fsm, size_t *length)
{
    *length = PAGE_SIZE - PAGE_HEADER_SIZE;
    return fsm->page->bytes + PAGE_HEADER_SIZE;
}
