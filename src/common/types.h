/**
 * Common types for TielDB
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

// Page and buffer pool types
typedef uint64_t page_id_t; // Page identifier
typedef uint64_t lsn_t;     // Log Sequence Number
typedef uint32_t txn_id_t;  // Transaction ID

// Tuple identifier (page + slot)
typedef struct tuple_id {
    page_id_t page_id;
    uint16_t slot_num;
} tuple_id_t;

// Common constants
enum {
    PAGE_SIZE = 8192, // 8 KB pages
    INVALID_PAGE_ID = UINT64_MAX,
    INVALID_LSN = 0,
    INVALID_TXN_ID = 0,
};

// Result codes
enum {
    TIELDB_OK = 0,
    TIELDB_ERROR = -1,
    TIELDB_NOMEM = -2,
    TIELDB_IO_ERROR = -3,
    TIELDB_FULL = -4,
};
