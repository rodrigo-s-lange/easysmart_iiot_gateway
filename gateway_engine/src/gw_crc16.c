#include "gw_crc16.h"

uint16_t gw_crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;

    for (i = 0; i < len; ++i) {
        uint8_t byte = data[i];
        int bit;

        crc ^= (uint16_t)byte << 8;
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}
