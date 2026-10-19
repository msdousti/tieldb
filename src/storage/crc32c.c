#include "storage/crc32c.h"

static constexpr uint32_t REFLECTED_POLYNOMIAL = 0x82F63B78;
static constexpr uint32_t INITIAL_VALUE = 0xFFFFFFFF;
static constexpr int BITS_PER_BYTE = 8;

static uint32_t update_with_byte(uint32_t crc, unsigned char byte)
{
    crc ^= byte;
    for (int bit = 0; bit < BITS_PER_BYTE; bit++) {
        crc = (crc >> 1) ^ (REFLECTED_POLYNOMIAL & (0U - (crc & 1U)));
    }
    return crc;
}

uint32_t crc32c(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    uint32_t crc = INITIAL_VALUE;
    for (size_t i = 0; i < length; i++) {
        crc = update_with_byte(crc, bytes[i]);
    }
    return ~crc;
}
