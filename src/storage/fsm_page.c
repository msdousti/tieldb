#include "storage/fsm_page.h"

#include <errno.h>

#include "storage/page_layout.h"

// The leaf count padded to a power of two, as max_tree.h defines. Padded leaves are not stored
static constexpr size_t PADDED_LEAVES = 4096;

static_assert(PADDED_LEAVES / 2 < FSM_PAGE_LEAVES && FSM_PAGE_LEAVES <= PADDED_LEAVES);
static_assert((PADDED_LEAVES - 1) + FSM_PAGE_LEAVES <= FSM_PAGE_BODY_SIZE);

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
    *length = FSM_PAGE_BODY_SIZE;
    return fsm->page->bytes + PAGE_HEADER_SIZE;
}

void fsm_page_tree(const fsm_page_t *fsm, max_tree_t *tree)
{
    size_t length = 0;
    unsigned char *body = fsm_page_body(fsm, &length);

    // The size is checked by the static_assert above, so the only error cannot happen
    (void) max_tree_view(tree, body, length, FSM_PAGE_LEAVES);
}

void fsm_page_rebuild_tree(const fsm_page_t *fsm, max_tree_t *tree)
{
    size_t length = 0;
    unsigned char *body = fsm_page_body(fsm, &length);

    // The size is checked by the static_assert above, so the only error cannot happen
    (void) max_tree_attach(tree, body, length, FSM_PAGE_LEAVES);
}
