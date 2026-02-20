#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

#include <gateway_engine/gw_engine.h>
#include <gateway_engine/gw_link_proto.h>

LOG_MODULE_REGISTER(hybrid_lighting_gateway, LOG_LEVEL_INF);

#define LAB_WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define LAB_WIFI_RETRY_MS 5000U
#define LAB_ENGINE_RETRY_MS 5000U

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
#define LAB_HAS_DEBUG_LED 1
#else
#define LAB_HAS_DEBUG_LED 0
#endif

typedef enum {
    DEBUG_LED_OFF = 0,
    DEBUG_LED_RED,
    DEBUG_LED_GREEN,
    DEBUG_LED_BLUE,
} debug_led_state_t;

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
    struct net_if *wifi_iface;
    struct net_mgmt_event_callback wifi_cb;
    bool wifi_connected;
    bool wifi_connect_pending;
    bool engine_started;
    uint32_t last_wifi_attempt_ms;
    uint32_t last_engine_attempt_ms;
    uint32_t last_scene_log_ms;
    debug_led_state_t led_state;
    const struct device *led_strip;
} lab_ctx_t;

static lab_ctx_t *g_lab_ctx;

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

static const struct device *lab_get_led_strip(void)
{
#if LAB_HAS_DEBUG_LED
    return DEVICE_DT_GET(DT_ALIAS(led_strip));
#else
    return NULL;
#endif
}

static int lab_set_debug_led(lab_ctx_t *lab, debug_led_state_t next)
{
    struct led_rgb pixel;
    int rc;

    if (lab == NULL || lab->led_strip == NULL) {
        return -ENODEV;
    }

    if (!device_is_ready(lab->led_strip)) {
        return -ENODEV;
    }

    if (lab->led_state == next) {
        return 0;
    }

    (void)memset(&pixel, 0, sizeof(pixel));

    switch (next) {
    case DEBUG_LED_RED:
        pixel.r = 64U;
        break;
    case DEBUG_LED_GREEN:
        pixel.g = 64U;
        break;
    case DEBUG_LED_BLUE:
        pixel.b = 64U;
        break;
    case DEBUG_LED_OFF:
    default:
        break;
    }

    rc = led_strip_update_rgb(lab->led_strip, &pixel, 1U);
    if (rc != 0) {
        return rc;
    }

    lab->led_state = next;
    return 0;
}

static void lab_refresh_debug_led(lab_ctx_t *lab)
{
    debug_led_state_t target = DEBUG_LED_RED;
    bool mqtt_connected;

    if (lab == NULL) {
        return;
    }

    mqtt_connected = lab->wifi_connected && lab->engine_started && lab->engine.cloud.connected;

    if (lab->wifi_connected && mqtt_connected) {
        target = DEBUG_LED_BLUE;
    } else if (lab->wifi_connected) {
        target = DEBUG_LED_GREEN;
    }

    if (lab_set_debug_led(lab, target) != 0) {
        return;
    }
}

static void lab_wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
    const struct wifi_status *status;
    lab_ctx_t *lab = g_lab_ctx;

    if (lab == NULL || iface != lab->wifi_iface) {
        return;
    }

    status = (const struct wifi_status *)cb->info;

    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        lab->wifi_connect_pending = false;
        if (status != NULL && status->status == 0) {
            lab->wifi_connected = true;
            LOG_INF("wifi connected");
        } else {
            lab->wifi_connected = false;
            LOG_WRN("wifi connect failed: %d", (status != NULL) ? status->status : -EIO);
        }
        break;

    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        lab->wifi_connected = false;
        lab->wifi_connect_pending = false;
        LOG_WRN("wifi disconnected: %d", (status != NULL) ? status->status : 0);
        break;

    default:
        break;
    }
}

static int lab_wifi_status_sync(lab_ctx_t *lab)
{
    struct wifi_iface_status status;
    int rc;

    if (lab == NULL || lab->wifi_iface == NULL) {
        return -EINVAL;
    }

    (void)memset(&status, 0, sizeof(status));

    rc = net_mgmt(
        NET_REQUEST_WIFI_IFACE_STATUS,
        lab->wifi_iface,
        &status,
        sizeof(struct wifi_iface_status));
    if (rc != 0) {
        return rc;
    }

    lab->wifi_connected = (status.state == WIFI_STATE_COMPLETED);
    return 0;
}

static int lab_wifi_connect_request(lab_ctx_t *lab)
{
    struct wifi_connect_req_params params;
    size_t ssid_len;
    size_t psk_len;

    if (lab == NULL || lab->wifi_iface == NULL) {
        return -EINVAL;
    }

    if (lab->wifi_connected || lab->wifi_connect_pending) {
        return 0;
    }

    ssid_len = strlen(CONFIG_HYBRID_WIFI_SSID);
    if (ssid_len == 0U) {
        return -ENODATA;
    }

    psk_len = strlen(CONFIG_HYBRID_WIFI_PSK);

    (void)memset(&params, 0, sizeof(params));
    params.ssid = (const uint8_t *)CONFIG_HYBRID_WIFI_SSID;
    params.ssid_length = (uint8_t)ssid_len;
    params.band = WIFI_FREQ_BAND_UNKNOWN;
    params.channel = WIFI_CHANNEL_ANY;
    params.mfp = WIFI_MFP_OPTIONAL;

    if (psk_len > 0U) {
        params.security = WIFI_SECURITY_TYPE_PSK;
        params.psk = (const uint8_t *)CONFIG_HYBRID_WIFI_PSK;
        params.psk_length = (uint8_t)psk_len;
    } else {
        params.security = WIFI_SECURITY_TYPE_NONE;
    }

    lab->wifi_connect_pending = true;

    return net_mgmt(
        NET_REQUEST_WIFI_CONNECT,
        lab->wifi_iface,
        &params,
        sizeof(struct wifi_connect_req_params));
}

static int lab_wifi_init(lab_ctx_t *lab)
{
    if (lab == NULL) {
        return -EINVAL;
    }

    lab->wifi_iface = net_if_get_default();
    if (lab->wifi_iface == NULL) {
        return -ENODEV;
    }

    g_lab_ctx = lab;

    net_mgmt_init_event_callback(&lab->wifi_cb, lab_wifi_event_handler, LAB_WIFI_EVENTS);
    net_mgmt_add_event_callback(&lab->wifi_cb);

    if (lab_wifi_status_sync(lab) == 0 && lab->wifi_connected) {
        LOG_INF("wifi already connected");
    }

    if (strlen(CONFIG_HYBRID_WIFI_SSID) == 0U) {
        LOG_WRN("CONFIG_HYBRID_WIFI_SSID is empty; wifi connect disabled");
    }

    return 0;
}

static void lab_wifi_poll(lab_ctx_t *lab, uint32_t now_ms)
{
    int rc;

    if (lab == NULL || lab->wifi_connected || lab->wifi_connect_pending) {
        return;
    }

    if (strlen(CONFIG_HYBRID_WIFI_SSID) == 0U) {
        return;
    }

    if ((now_ms - lab->last_wifi_attempt_ms) < LAB_WIFI_RETRY_MS) {
        return;
    }

    lab->last_wifi_attempt_ms = now_ms;

    rc = lab_wifi_connect_request(lab);
    if (rc != 0) {
        lab->wifi_connect_pending = false;
        LOG_WRN("wifi connect request failed: %d", rc);
    } else {
        LOG_INF("wifi connect requested");
    }
}

static void lab_engine_poll(lab_ctx_t *lab, uint32_t now_ms)
{
    int rc;

    if (lab == NULL) {
        return;
    }

    if (!lab->wifi_connected) {
        if (lab->engine_started) {
            (void)gw_engine_stop(&lab->engine);
            lab->engine_started = false;
            LOG_WRN("engine stopped due to wifi disconnect");
        }
        return;
    }

    if (lab->engine_started) {
        return;
    }

    if ((now_ms - lab->last_engine_attempt_ms) < LAB_ENGINE_RETRY_MS) {
        return;
    }

    lab->last_engine_attempt_ms = now_ms;

    rc = gw_engine_start(&lab->engine);
    if (rc == 0) {
        lab->engine_started = true;
        LOG_INF("engine started");
        return;
    }

    LOG_WRN("engine start failed: %d", rc);
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
    lab->led_state = DEBUG_LED_OFF;

    lab->led_strip = lab_get_led_strip();
    if (lab->led_strip != NULL && !device_is_ready(lab->led_strip)) {
        LOG_WRN("debug led strip present but not ready");
        lab->led_strip = NULL;
    }

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

    rc = lab_wifi_init(lab);
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
        uint32_t now_ms = k_uptime_get_32();

        lab_wifi_poll(&lab, now_ms);
        lab_engine_poll(&lab, now_ms);
        lab_refresh_debug_led(&lab);

        edge_simulate_animation(&lab.edge);

        if (lab.engine_started && (loop_count % 10U) == 0U) {
            rc = gateway_send_heartbeat(&lab.engine);
            if (rc != 0) {
                LOG_WRN("heartbeat send failed: %d", rc);
            }
        }

        if (lab.engine_started && (loop_count % 5U) == 0U) {
            rc = gateway_send_lighting_telemetry(&lab.engine, &lab.edge);
            if (rc != 0) {
                LOG_WRN("telemetry send failed: %d", rc);
            }
        }

        if (lab.engine_started && (loop_count % 200U) == 0U) {
            uint8_t next_scene = (uint8_t)((lab.edge.scene + 1U) % 3U);
            rc = gateway_send_control(&lab.engine, 0x03, next_scene);
            if (rc == 0) {
                LOG_INF("scene changed to %u", (unsigned int)next_scene);
            }
        }

        if (lab.engine_started) {
            rc = gw_engine_step(&lab.engine);
            if (rc != 0) {
                LOG_WRN("engine step failed: %d", rc);
                (void)gw_engine_stop(&lab.engine);
                lab.engine_started = false;
            }
        }

        if ((now_ms - lab.last_scene_log_ms) >= 1000U) {
            lab.last_scene_log_ms = now_ms;
            LOG_INF(
                "edge state on=%u brightness=%u scene=%u hb=%u wifi=%u mqtt=%u",
                (unsigned int)lab.edge.is_on,
                (unsigned int)lab.edge.brightness,
                (unsigned int)lab.edge.scene,
                (unsigned int)lab.edge.heartbeat_count,
                (unsigned int)lab.wifi_connected,
                (unsigned int)(lab.engine_started && lab.engine.cloud.connected));
        }

        loop_count++;
        k_sleep(K_MSEC(20));
    }

    (void)gw_engine_stop(&lab.engine);
    return 0;
}
