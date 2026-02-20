#include "gw_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t block[64];
    size_t block_len;
} gw_sha256_ctx_t;

static const uint32_t GW_SHA256_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t big_sigma0(uint32_t x)
{
    return rotr32(x, 2U) ^ rotr32(x, 13U) ^ rotr32(x, 22U);
}

static uint32_t big_sigma1(uint32_t x)
{
    return rotr32(x, 6U) ^ rotr32(x, 11U) ^ rotr32(x, 25U);
}

static uint32_t small_sigma0(uint32_t x)
{
    return rotr32(x, 7U) ^ rotr32(x, 18U) ^ (x >> 3U);
}

static uint32_t small_sigma1(uint32_t x)
{
    return rotr32(x, 17U) ^ rotr32(x, 19U) ^ (x >> 10U);
}

static uint32_t read_u32_be(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0] << 24U) |
           ((uint32_t)ptr[1] << 16U) |
           ((uint32_t)ptr[2] << 8U) |
           ((uint32_t)ptr[3]);
}

static void write_u32_be(uint8_t *ptr, uint32_t value)
{
    ptr[0] = (uint8_t)(value >> 24U);
    ptr[1] = (uint8_t)(value >> 16U);
    ptr[2] = (uint8_t)(value >> 8U);
    ptr[3] = (uint8_t)value;
}

static void write_u64_be(uint8_t *ptr, uint64_t value)
{
    ptr[0] = (uint8_t)(value >> 56U);
    ptr[1] = (uint8_t)(value >> 48U);
    ptr[2] = (uint8_t)(value >> 40U);
    ptr[3] = (uint8_t)(value >> 32U);
    ptr[4] = (uint8_t)(value >> 24U);
    ptr[5] = (uint8_t)(value >> 16U);
    ptr[6] = (uint8_t)(value >> 8U);
    ptr[7] = (uint8_t)value;
}

static void gw_sha256_transform(gw_sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t t1;
    uint32_t t2;
    int i;

    for (i = 0; i < 16; ++i) {
        w[i] = read_u32_be(&block[i * 4]);
    }

    for (i = 16; i < 64; ++i) {
        w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + big_sigma1(e) + ch(e, f, g) + GW_SHA256_K[i] + w[i];
        t2 = big_sigma0(a) + maj(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void gw_sha256_init(gw_sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bit_len = 0U;
    ctx->block_len = 0U;
}

static void gw_sha256_update(gw_sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    if (data == NULL || len == 0U) {
        return;
    }

    for (i = 0; i < len; ++i) {
        ctx->block[ctx->block_len++] = data[i];
        if (ctx->block_len == 64U) {
            gw_sha256_transform(ctx, ctx->block);
            ctx->bit_len += 512U;
            ctx->block_len = 0U;
        }
    }
}

static void gw_sha256_final(gw_sha256_ctx_t *ctx, uint8_t out_digest[32])
{
    size_t i;

    ctx->bit_len += (uint64_t)ctx->block_len * 8U;

    ctx->block[ctx->block_len++] = 0x80U;

    if (ctx->block_len > 56U) {
        while (ctx->block_len < 64U) {
            ctx->block[ctx->block_len++] = 0x00U;
        }
        gw_sha256_transform(ctx, ctx->block);
        ctx->block_len = 0U;
    }

    while (ctx->block_len < 56U) {
        ctx->block[ctx->block_len++] = 0x00U;
    }

    write_u64_be(&ctx->block[56], ctx->bit_len);
    gw_sha256_transform(ctx, ctx->block);

    for (i = 0; i < 8U; ++i) {
        write_u32_be(&out_digest[i * 4U], ctx->state[i]);
    }
}

void gw_sha256(const uint8_t *data, size_t len, uint8_t out_digest[32])
{
    gw_sha256_ctx_t ctx;

    gw_sha256_init(&ctx);
    gw_sha256_update(&ctx, data, len);
    gw_sha256_final(&ctx, out_digest);
}

void gw_hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *msg,
    size_t msg_len,
    uint8_t out_digest[32])
{
    uint8_t key_block[64];
    uint8_t inner_digest[32];
    uint8_t ipad[64];
    uint8_t opad[64];
    gw_sha256_ctx_t ctx;
    size_t i;

    (void)memset(key_block, 0, sizeof(key_block));

    if (key != NULL && key_len > 0U) {
        if (key_len > sizeof(key_block)) {
            gw_sha256(key, key_len, key_block);
        } else {
            (void)memcpy(key_block, key, key_len);
        }
    }

    for (i = 0; i < sizeof(key_block); ++i) {
        ipad[i] = (uint8_t)(key_block[i] ^ 0x36U);
        opad[i] = (uint8_t)(key_block[i] ^ 0x5cU);
    }

    gw_sha256_init(&ctx);
    gw_sha256_update(&ctx, ipad, sizeof(ipad));
    gw_sha256_update(&ctx, msg, msg_len);
    gw_sha256_final(&ctx, inner_digest);

    gw_sha256_init(&ctx);
    gw_sha256_update(&ctx, opad, sizeof(opad));
    gw_sha256_update(&ctx, inner_digest, sizeof(inner_digest));
    gw_sha256_final(&ctx, out_digest);
}
