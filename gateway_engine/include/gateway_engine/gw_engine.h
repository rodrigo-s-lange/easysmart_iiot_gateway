#ifndef GW_ENGINE_H
#define GW_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <gateway_engine/gw_cloud.h>
#include <gateway_engine/gw_ota.h>
#include <gateway_engine/gw_profile.h>
#include <gateway_engine/gw_transport.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GW_ENGINE_STATE_INIT = 0,
    GW_ENGINE_STATE_READY = 1,
    GW_ENGINE_STATE_RUNNING = 2,
    GW_ENGINE_STATE_FAULT = 3,
} gw_engine_state_t;

typedef struct {
    gw_profile_t profile;
    const char *device_id;
    uint32_t loop_period_ms;
    gw_cloud_config_t cloud;
    gw_ota_config_t ota;
} gw_engine_config_t;

typedef struct {
    gw_engine_config_t config;
    gw_transport_t transport;
    gw_cloud_client_t cloud;
    gw_ota_ctx_t ota;
    gw_engine_state_t state;
    uint16_t tx_seq;
    bool initialized;
    bool running;
} gw_engine_t;

int gw_engine_init(gw_engine_t *engine, const gw_engine_config_t *cfg, const gw_transport_t *transport);
int gw_engine_start(gw_engine_t *engine);
int gw_engine_step(gw_engine_t *engine);
int gw_engine_send(gw_engine_t *engine, uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
int gw_engine_stop(gw_engine_t *engine);
const char *gw_engine_profile_name(const gw_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif
