#ifndef GW_OTA_H
#define GW_OTA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GW_OTA_STATE_IDLE = 0,
    GW_OTA_STATE_RECEIVING = 1,
    GW_OTA_STATE_VERIFYING = 2,
    GW_OTA_STATE_READY_TO_APPLY = 3,
} gw_ota_state_t;

typedef struct {
    uint32_t chunk_size;
    uint32_t timeout_ms;
} gw_ota_config_t;

typedef struct {
    gw_ota_config_t config;
    gw_ota_state_t state;
    uint32_t bytes_received;
} gw_ota_ctx_t;

int gw_ota_init(gw_ota_ctx_t *ctx, const gw_ota_config_t *cfg);
int gw_ota_begin(gw_ota_ctx_t *ctx);
int gw_ota_push_chunk(gw_ota_ctx_t *ctx, const uint8_t *chunk, size_t chunk_len);
int gw_ota_finish(gw_ota_ctx_t *ctx);
int gw_ota_pump(gw_ota_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
