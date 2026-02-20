#ifndef GW_SHA256_H
#define GW_SHA256_H

#include <stddef.h>
#include <stdint.h>

void gw_sha256(const uint8_t *data, size_t len, uint8_t out_digest[32]);
void gw_hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *msg,
    size_t msg_len,
    uint8_t out_digest[32]);

#endif
