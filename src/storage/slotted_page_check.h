/**
 * The check of the slots of a page, for page_validate. Private to page.c and slotted_page.c; the
 * other modules use page_validate.
 */

#pragma once

#include "storage/page.h"

/**
 * Checks that the slots of a page of a slotted type are consistent, so that the slotted page
 * functions can trust the page. It does not check the page type, so page_validate calls it for
 * slotted types only. EBADMSG: the page is corrupt.
 */
[[nodiscard]] int slotted_page_validate(const page_t *page);
