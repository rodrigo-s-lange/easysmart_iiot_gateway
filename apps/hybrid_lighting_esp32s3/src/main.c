#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <gateway_engine/gw_engine.h>
#include <gateway_engine/gw_link_proto.h>

LOG_MODULE_REGISTER(hybrid_lighting_gateway, LOG_LEVEL_INF);

typedef struct {
    uint8_t is_on;
    uint8_t brightness;
    uint8_t scene;
    uint16_t heartbeat_count;
    uint32_t tick;
} edge_lighting_state_t;

typedef struct {
    edge_lighting_state_t edge;
    gw_engine_t engine;
    gw_transport_t transport;
    gw_transport_internal_t internal_backend;
    uint32_t last_scene_log_ms;
} lab_ctx_t;

static int edge_build_ack(const gw_link_frame_view_t *view, uint8_t *rx_data, size_t rx_cap, size_t *rx_len)
{
    uint8_t payload[4] = {0U, 0U, 0U, 0U};

    if (view == NULL || rx_data == NULL || rx_len == NULL) {
        return -EINVAL;
    }

    payload[0] = view->cmd;
    payload[1] = (uint8_t)(view->seq & 0x00FFU);
    payload[2] = (uint8_t)(view->seq >> 8);
    payload[3] = 0x00U;

    return gw_link_encode(0U, GW_LINK_CMD_ACK, view->seq, payload, sizeof(payload), rx_data, rx_cap, rx_len);
}

static void edge_apply_control(edge_lighting_state_t *edge, const gw_link_frame_view_t *view)
{
    uint8_t op;
    uint8_t arg;

    if (edge == NULL || view == NULL || view->payload_len < 2U) {
        return;
    }

    op = view->payload[0];
    arg = view->payload[1];

    switch (op) {
    case 0x01:
        edge->is_on = (arg > 0U) ? 1U : 0U;
        break;
    case 0x02:
        edge->brightness = arg;
        break;
    case 0x03:
        edge->scene = arg;
        break;
    default:
        break;
    }
}

static int internal_exchange_cb(
    const uint8_t *tx_data,
    size_t tx_len,
    uint8_t *rx_data,
    size_t rx_cap,
    size_t *rx_len,
    void *user_data)
{
    lab_ctx_t *lab = (lab_ctx_t *)user_data;
    gw_link_frame_view_t view;
    int rc;

    if (lab == NULL || tx_data == NULL || tx_len == 0U || rx_data == NULL || rx_len == NULL) {
        return -EINVAL;
    }

    rc = gw_link_decode(tx_data, tx_len, &view);
    if (rc != 0) {
        return rc;
    }

    if (view.cmd == GW_LINK_CMD_HEARTBEAT) {
        lab->edge.heartbeat_count++;
    } else if (view.cmd == GW_LINK_CMD_CONTROL) {
        edge_apply_control(&lab->edge, &view);
    }

    return edge_build_ack(&view, rx_data, rx_cap, rx_len);
}

static void edge_simulate_animation(edge_lighting_state_t *edge)
{
    if (edge == NULL) {
        return;
    }

    edge->tick++;

    if (edge->is_on == 0U) {
        edge->brightness = 0U;
        return;
    }

    if (edge->scene == 0U) {
        edge->brightness = (uint8_t)(30U + (edge->tick % 70U));
    } else if (edge->scene == 1U) {
        edge->brightness = (uint8_t)(edge->tick % 100U);
    } else {
        edge->brightness = (uint8_t)(80U + ((edge->tick / 8U) % 20U));
    }
}

static int gateway_send_control(gw_engine_t *engine, uint8_t op, uint8_t arg)
{
    uint8_t payload[2];

    if (engine == NULL) {
        return -EINVAL;
    }

    payload[0] = op;
    payload[1] = arg;

    return gw_engine_send(engine, GW_LINK_CMD_CONTROL, payload, sizeof(payload));
}

static int gateway_send_heartbeat(gw_engine_t *engine)
{
    uint8_t payload[2] = {0xAAU, 0x55U};

    if (engine == NULL) {
        return -EINVAL;
    }

    return gw_engine_send(engine, GW_LINK_CMD_HEARTBEAT, payload, sizeof(payload));
}

static int gateway_send_lighting_telemetry(gw_engine_t *engine, const edge_lighting_state_t *edge)
{
    char payload[96];
    int n;

    if (engine == NULL || edge == NULL) {
        return -EINVAL;
    }

    n = snprintk(
        payload,
        sizeof(payload),
        "{\"on\":%u,\"brightness\":%u,\"scene\":%u,\"hb\":%u}",
        (unsigned int)edge->is_on,
        (unsigned int)edge->brightness,
        (unsigned int)edge->scene,
        (unsigned int)edge->heartbeat_count);
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return -ENOSPC;
    }

    return gw_engine_send(engine, GW_LINK_CMD_TELEMETRY, (const uint8_t *)payload, (uint16_t)n);
}

static int lab_init(lab_ctx_t *lab)
{
    gw_transport_internal_config_t internal_cfg;
    gw_engine_config_t engine_cfg;
    int rc;

    if (lab == NULL) {
        return -EINVAL;
    }

    (void)memset(lab, 0, sizeof(*lab));

    lab->edge.is_on = 1U;
    lab->edge.scene = 0U;
    lab->edge.brightness = 40U;

    internal_cfg.exchange_cb = internal_exchange_cb;
    internal_cfg.user_data = lab;
    internal_cfg.mtu = 512U;

    rc = gw_transport_internal_init(&lab->internal_backend, &lab->transport, &internal_cfg);
    if (rc != 0) {
        return rc;
    }

    (void)memset(&engine_cfg, 0, sizeof(engine_cfg));
    engine_cfg.profile = GW_PROFILE_LIGHTING_GATEWAY;
    engine_cfg.device_id = "hybrid-lighting-esp32s3";
    engine_cfg.loop_period_ms = 20U;

    engine_cfg.cloud.device_id = "hybrid-lighting-esp32s3";
    engine_cfg.cloud.hardware_id = "3030F903AA1C";
    engine_cfg.cloud.identity_key = "3030F903AA1C";
    engine_cfg.cloud.manufacturing_key = "lab-key";
    engine_cfg.cloud.bootstrap_timeout_ms = 1000U;
    engine_cfg.cloud.mqtt_connect_timeout_ms = 1000U;

    engine_cfg.ota.chunk_size = 1024U;
    engine_cfg.ota.timeout_ms = 3000U;

    rc = gw_engine_init(&lab->engine, &engine_cfg, &lab->transport);
    if (rc != 0) {
        return rc;
    }

    rc = gw_engine_start(&lab->engine);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int main(void)
{
    lab_ctx_t lab;
    uint32_t loop_count = 0U;
    int rc;

    rc = lab_init(&lab);
    if (rc != 0) {
        LOG_ERR("lab_init failed: %d", rc);
        return rc;
    }

    LOG_INF("Hybrid lighting lab started on profile=%s", gw_engine_profile_name(&lab.engine));

    while (true) {
        int64_t now_ms = k_uptime_get();

        edge_simulate_animation(&lab.edge);

        if ((loop_count % 10U) == 0U) {
            rc = gateway_send_heartbeat(&lab.engine);
            if (rc != 0) {
                LOG_WRN("heartbeat send failed: %d", rc);
            }
        }

        if ((loop_count % 5U) == 0U) {
            rc = gateway_send_lighting_telemetry(&lab.engine, &lab.edge);
            if (rc != 0) {
                LOG_WRN("telemetry send failed: %d", rc);
            }
        }

        if ((loop_count % 200U) == 0U) {
            uint8_t next_scene = (uint8_t)((lab.edge.scene + 1U) % 3U);
            rc = gateway_send_control(&lab.engine, 0x03, next_scene);
            if (rc == 0) {
                LOG_INF("scene changed to %u", (unsigned int)next_scene);
            }
        }

        rc = gw_engine_step(&lab.engine);
        if (rc != 0) {
            LOG_ERR("engine step failed: %d", rc);
            break;
        }

        if ((now_ms - (int64_t)lab.last_scene_log_ms) >= 1000) {
            lab.last_scene_log_ms = (uint32_t)now_ms;
            LOG_INF(
                "edge state on=%u brightness=%u scene=%u hb=%u",
                (unsigned int)lab.edge.is_on,
                (unsigned int)lab.edge.brightness,
                (unsigned int)lab.edge.scene,
                (unsigned int)lab.edge.heartbeat_count);
        }

        loop_count++;
        k_sleep(K_MSEC(20));
    }

    (void)gw_engine_stop(&lab.engine);
    return 0;
}
