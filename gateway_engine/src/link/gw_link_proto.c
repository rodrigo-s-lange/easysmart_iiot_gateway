#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <gateway_engine/gw_link_proto.h>

#include "../gw_crc16.h"

static uint16_t read_u16_le(const uint8_t *ptr)
{
    return (uint16_t)((uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8));
}

static void write_u16_le(uint8_t *ptr, uint16_t value)
{
    ptr[0] = (uint8_t)(value & 0x00FFU);
    ptr[1] = (uint8_t)(value >> 8);
}

int gw_link_encode(
    uint8_t flags,
    uint8_t cmd,
    uint16_t seq,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *out_buf,
    size_t out_cap,
    size_t *out_len)
{
    size_t frame_len;
    uint16_t crc;

    if (out_buf == NULL || out_len == NULL) {
        return -EINVAL;
    }

    if (payload_len > GW_LINK_MAX_PAYLOAD) {
        return -EMSGSIZE;
    }

    if (payload_len > 0U && payload == NULL) {
        return -EINVAL;
    }

    frame_len = GW_LINK_HEADER_SIZE + (size_t)payload_len + GW_LINK_CRC_SIZE;
    if (out_cap < frame_len) {
        return -ENOBUFS;
    }

    out_buf[0] = GW_LINK_SOF;
    out_buf[1] = GW_LINK_VERSION;
    out_buf[2] = flags;
    out_buf[3] = cmd;
    write_u16_le(&out_buf[4], seq);
    write_u16_le(&out_buf[6], payload_len);

    if (payload_len > 0U) {
        (void)memcpy(&out_buf[GW_LINK_HEADER_SIZE], payload, payload_len);
    }

    crc = gw_crc16_ccitt_false(&out_buf[1], (size_t)(GW_LINK_HEADER_SIZE - 1U) + payload_len);
    write_u16_le(&out_buf[GW_LINK_HEADER_SIZE + payload_len], crc);

    *out_len = frame_len;
    return 0;
}

int gw_link_decode(const uint8_t *frame, size_t frame_len, gw_link_frame_view_t *out_view)
{
    uint16_t payload_len;
    size_t expected_len;
    uint16_t rx_crc;
    uint16_t calc_crc;

    if (frame == NULL || out_view == NULL) {
        return -EINVAL;
    }

    if (frame_len < (GW_LINK_HEADER_SIZE + GW_LINK_CRC_SIZE)) {
        return -EMSGSIZE;
    }

    if (frame[0] != GW_LINK_SOF || frame[1] != GW_LINK_VERSION) {
        return -EPROTO;
    }

    payload_len = read_u16_le(&frame[6]);
    if (payload_len > GW_LINK_MAX_PAYLOAD) {
        return -EMSGSIZE;
    }

    expected_len = GW_LINK_HEADER_SIZE + (size_t)payload_len + GW_LINK_CRC_SIZE;
    if (frame_len != expected_len) {
        return -EMSGSIZE;
    }

    rx_crc = read_u16_le(&frame[GW_LINK_HEADER_SIZE + payload_len]);
    calc_crc = gw_crc16_ccitt_false(&frame[1], (size_t)(GW_LINK_HEADER_SIZE - 1U) + payload_len);

    if (rx_crc != calc_crc) {
        return -EBADMSG;
    }

    out_view->flags = frame[2];
    out_view->cmd = frame[3];
    out_view->seq = read_u16_le(&frame[4]);
    out_view->payload_len = payload_len;
    out_view->payload = &frame[GW_LINK_HEADER_SIZE];

    return 0;
}
