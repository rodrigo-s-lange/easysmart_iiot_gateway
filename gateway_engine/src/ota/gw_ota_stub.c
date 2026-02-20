#include <errno.h>

#include <gateway_engine/gw_ota.h>

int gw_ota_init(gw_ota_ctx_t *ctx, const gw_ota_config_t *cfg)
{
    if (ctx == NULL || cfg == NULL) {
        return -EINVAL;
    }

    ctx->config = *cfg;
    ctx->state = GW_OTA_STATE_IDLE;
    ctx->bytes_received = 0U;

    return 0;
}

int gw_ota_begin(gw_ota_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    if (ctx->state != GW_OTA_STATE_IDLE) {
        return -EALREADY;
    }

    ctx->state = GW_OTA_STATE_RECEIVING;
    ctx->bytes_received = 0U;
    return 0;
}

int gw_ota_push_chunk(gw_ota_ctx_t *ctx, const uint8_t *chunk, size_t chunk_len)
{
    if (ctx == NULL || chunk == NULL || chunk_len == 0U) {
        return -EINVAL;
    }

    if (ctx->state != GW_OTA_STATE_RECEIVING) {
        return -EPERM;
    }

    if (ctx->config.chunk_size > 0U && chunk_len > ctx->config.chunk_size) {
        return -EMSGSIZE;
    }

    ctx->bytes_received += (uint32_t)chunk_len;
    return 0;
}

int gw_ota_finish(gw_ota_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    if (ctx->state != GW_OTA_STATE_RECEIVING) {
        return -EPERM;
    }

    ctx->state = GW_OTA_STATE_VERIFYING;
    ctx->state = GW_OTA_STATE_READY_TO_APPLY;
    return 0;
}

int gw_ota_pump(gw_ota_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    return 0;
}
