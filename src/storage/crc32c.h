/**
 * CRC-32C (Castagnoli), the checksum of the pages on disk.
 *
 * The standard algorithm, with the usual check value 0xE3069283 for the text "123456789".
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

[[nodiscard]] uint32_t crc32c(const void *data, size_t length);
