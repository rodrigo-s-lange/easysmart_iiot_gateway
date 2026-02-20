#ifndef GW_CRC16_H
#define GW_CRC16_H

#include <stddef.h>
#include <stdint.h>

uint16_t gw_crc16_ccitt_false(const uint8_t *data, size_t len);

#endif
