#ifndef GW_LINK_PROTO_H
#define GW_LINK_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GW_LINK_SOF 0xA5U
#define GW_LINK_VERSION 0x01U
#define GW_LINK_HEADER_SIZE 8U
#define GW_LINK_CRC_SIZE 2U
#define GW_LINK_MAX_PAYLOAD 512U
#define GW_LINK_MAX_FRAME_SIZE (GW_LINK_HEADER_SIZE + GW_LINK_MAX_PAYLOAD + GW_LINK_CRC_SIZE)

typedef enum {
    GW_LINK_CMD_NOP = 0x00,
    GW_LINK_CMD_HEARTBEAT = 0x01,
    GW_LINK_CMD_TELEMETRY = 0x10,
    GW_LINK_CMD_CONTROL = 0x11,
    GW_LINK_CMD_OTA_BEGIN = 0x20,
    GW_LINK_CMD_OTA_CHUNK = 0x21,
    GW_LINK_CMD_OTA_END = 0x22,
    GW_LINK_CMD_ACK = 0x7E,
    GW_LINK_CMD_NACK = 0x7F,
} gw_link_cmd_t;

typedef struct {
    uint8_t flags;
    uint8_t cmd;
    uint16_t seq;
    uint16_t payload_len;
    const uint8_t *payload;
} gw_link_frame_view_t;

int gw_link_encode(
    uint8_t flags,
    uint8_t cmd,
    uint16_t seq,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *out_buf,
    size_t out_cap,
    size_t *out_len);

int gw_link_decode(const uint8_t *frame, size_t frame_len, gw_link_frame_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif
