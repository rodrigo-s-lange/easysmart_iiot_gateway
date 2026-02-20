#ifndef GW_CLOUD_H
#define GW_CLOUD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *api_base_url;
    const char *bootstrap_url;
    const char *secret_url;
    const char *broker_url;
    const char *tenant_id;
    const char *device_id;
    const char *hardware_id;
    const char *identity_key;
    const char *manufacturing_key;
    const char *mqtt_username;
    const char *device_secret;
    const char *topic_prefix;
    const char *mqtt_client_id;
    int tls_sec_tag;
    uint16_t mqtt_keepalive_sec;
    uint32_t bootstrap_timeout_ms;
    uint32_t mqtt_connect_timeout_ms;
} gw_cloud_config_t;

typedef enum {
    GW_CLOUD_STATUS_UNKNOWN = 0,
    GW_CLOUD_STATUS_NOT_PROVISIONED = 1,
    GW_CLOUD_STATUS_UNCLAIMED = 2,
    GW_CLOUD_STATUS_CLAIMED = 3,
    GW_CLOUD_STATUS_ACTIVE = 4,
    GW_CLOUD_STATUS_SUSPENDED = 5,
    GW_CLOUD_STATUS_REVOKED = 6,
} gw_cloud_status_t;

typedef struct {
    gw_cloud_config_t config;
    bool initialized;
    bool connected;
    bool credentials_ready;
    gw_cloud_status_t status;
    uint32_t poll_interval_s;
    char resolved_device_id[64];
    char resolved_hardware_id[16];
    char resolved_broker[160];
    char resolved_mqtt_username[80];
    char resolved_device_secret[96];
    char resolved_topic_prefix[192];
} gw_cloud_client_t;

int gw_cloud_init(gw_cloud_client_t *client, const gw_cloud_config_t *cfg);
int gw_cloud_connect(gw_cloud_client_t *client);
int gw_cloud_publish_telemetry(gw_cloud_client_t *client, const uint8_t *payload, size_t payload_len);
int gw_cloud_pump(gw_cloud_client_t *client);
int gw_cloud_disconnect(gw_cloud_client_t *client);

#ifdef __cplusplus
}
#endif

#endif
