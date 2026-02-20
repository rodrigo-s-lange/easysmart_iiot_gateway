#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <gateway_engine/gw_engine.h>
#include <gateway_engine/gw_link_proto.h>

static int handle_incoming_frame(gw_engine_t *engine, const uint8_t *frame, size_t frame_len)
{
    gw_link_frame_view_t view;
    int rc;

    rc = gw_link_decode(frame, frame_len, &view);
    if (rc != 0) {
        return rc;
    }

    if (view.cmd == GW_LINK_CMD_OTA_BEGIN) {
        return gw_ota_begin(&engine->ota);
    }

    if (view.cmd == GW_LINK_CMD_OTA_CHUNK) {
        return gw_ota_push_chunk(&engine->ota, view.payload, view.payload_len);
    }

    if (view.cmd == GW_LINK_CMD_OTA_END) {
        return gw_ota_finish(&engine->ota);
    }

    return 0;
}

int gw_engine_init(gw_engine_t *engine, const gw_engine_config_t *cfg, const gw_transport_t *transport)
{
    int rc;

    if (engine == NULL || cfg == NULL || transport == NULL) {
        return -EINVAL;
    }

    if (cfg->device_id == NULL || cfg->loop_period_ms == 0U) {
        return -EINVAL;
    }

    (void)memset(engine, 0, sizeof(*engine));
    engine->config = *cfg;
    engine->transport = *transport;
    engine->state = GW_ENGINE_STATE_INIT;
    engine->tx_seq = 1U;

    rc = gw_cloud_init(&engine->cloud, &cfg->cloud);
    if (rc != 0) {
        engine->state = GW_ENGINE_STATE_FAULT;
        return rc;
    }

    rc = gw_ota_init(&engine->ota, &cfg->ota);
    if (rc != 0) {
        engine->state = GW_ENGINE_STATE_FAULT;
        return rc;
    }

    engine->state = GW_ENGINE_STATE_READY;
    engine->initialized = true;
    return 0;
}

int gw_engine_start(gw_engine_t *engine)
{
    int rc;

    if (engine == NULL || !engine->initialized) {
        return -EINVAL;
    }

    if (engine->running) {
        return 0;
    }

    rc = gw_transport_open(&engine->transport);
    if (rc != 0) {
        engine->state = GW_ENGINE_STATE_FAULT;
        return rc;
    }

    rc = gw_cloud_connect(&engine->cloud);
    if (rc != 0) {
        (void)gw_transport_close(&engine->transport);
        engine->state = GW_ENGINE_STATE_FAULT;
        return rc;
    }

    engine->running = true;
    engine->state = GW_ENGINE_STATE_RUNNING;
    return 0;
}

int gw_engine_step(gw_engine_t *engine)
{
    uint8_t rx_buf[GW_LINK_MAX_FRAME_SIZE];
    size_t rx_len = 0U;
    int rc;

    if (engine == NULL || !engine->running) {
        return -EINVAL;
    }

    rc = gw_transport_rx(&engine->transport, rx_buf, sizeof(rx_buf), &rx_len, 0U);
    if (rc == 0 && rx_len > 0U) {
        rc = handle_incoming_frame(engine, rx_buf, rx_len);
        if (rc != 0) {
            engine->state = GW_ENGINE_STATE_FAULT;
            return rc;
        }
    }

    rc = gw_cloud_pump(&engine->cloud);
    if (rc != 0 && rc != -ENOTCONN) {
        engine->state = GW_ENGINE_STATE_FAULT;
        return rc;
    }

    rc = gw_ota_pump(&engine->ota);
    if (rc != 0) {
        engine->state = GW_ENGINE_STATE_FAULT;
        return rc;
    }

    return 0;
}

int gw_engine_send(gw_engine_t *engine, uint8_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[GW_LINK_MAX_FRAME_SIZE];
    size_t frame_len = 0U;
    int rc;

    if (engine == NULL || !engine->running) {
        return -EINVAL;
    }

    rc = gw_link_encode(0U, cmd, engine->tx_seq++, payload, payload_len, frame, sizeof(frame), &frame_len);
    if (rc != 0) {
        return rc;
    }

    return gw_transport_tx(&engine->transport, frame, frame_len, engine->config.loop_period_ms);
}

int gw_engine_stop(gw_engine_t *engine)
{
    if (engine == NULL || !engine->initialized) {
        return -EINVAL;
    }

    (void)gw_cloud_disconnect(&engine->cloud);
    (void)gw_transport_close(&engine->transport);

    engine->running = false;
    engine->state = GW_ENGINE_STATE_READY;

    return 0;
}

const char *gw_engine_profile_name(const gw_engine_t *engine)
{
    if (engine == NULL) {
        return "unknown";
    }

    return gw_profile_name(engine->config.profile);
}
