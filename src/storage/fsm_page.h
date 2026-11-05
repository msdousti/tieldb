/**
 * Free space map pages: the pages of the file in which the free space map keeps its max-tree.
 *
 * A free space map page has the common page header (page.h) and no slots. All the bytes after the
 * header are the body, which the map uses as it likes, for instance as the memory of a max-tree
 * (ds/max_tree.h). The checksum covers the body, so a torn page is found when it is read. The
 * header check of page_validate accepts any body.
 *
 * An fsm_page_t is a view of a page_t of this type. It holds no resources and needs no destroy
 * function. The page stays with the caller, who keeps it alive while the view is used. The
 * functions do no locking.
 *
 * Errors return -1 and set errno: EINVAL for a page of another type.
 */

#pragma once

#include <stddef.h>

#include "ds/max_tree.h"
#include "storage/page.h"

// The size of the body: all the bytes after the header
constexpr size_t FSM_PAGE_BODY_SIZE = PAGE_SIZE - PAGE_HEADER_SIZE;

/**
 * The number of leaves of the max-tree in the body: one per heap page that the page can describe.
 * With the padding that is not stored, the tree needs 8,165 of the 8,168 bytes of the body.
 */
constexpr size_t FSM_PAGE_LEAVES = 4070;

typedef struct {
    page_t *page;
} fsm_page_t;

/** Turns page into an empty free space map page, with a body of zeros, and makes fsm a view of it.
 */
void fsm_page_init(fsm_page_t *fsm, page_t *page);

/** Makes fsm a view of page. EINVAL: the page is not a free space map page. */
[[nodiscard]] int fsm_page_open(fsm_page_t *fsm, page_t *page);

/** The body of the page: the bytes after the header. Its length is put in *length. */
[[nodiscard]] unsigned char *fsm_page_body(const fsm_page_t *fsm, size_t *length);

/**
 * Makes tree a view of the FSM_PAGE_LEAVES leaves in the body (see max_tree_view). It trusts the
 * inner nodes, so it is for a page that the map wrote and read back with a checked read. It reads
 * no byte and costs the same however many leaves there are. This cannot fail: the body is large
 * enough for the tree, which fsm_page.c checks when it is compiled. The tree points into the
 * page, so it is valid as long as the page is.
 */
void fsm_page_tree(const fsm_page_t *fsm, max_tree_t *tree);
