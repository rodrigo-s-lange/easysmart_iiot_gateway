#include <errno.h>

#include <gateway_engine/gw_cloud.h>

int gw_cloud_init(gw_cloud_client_t *client, const gw_cloud_config_t *cfg)
{
    if (client == NULL || cfg == NULL) {
        return -EINVAL;
    }

    client->config = *cfg;
    client->initialized = true;
    client->connected = false;

    return 0;
}

int gw_cloud_connect(gw_cloud_client_t *client)
{
    if (client == NULL || !client->initialized) {
        return -EINVAL;
    }

    client->connected = true;
    return 0;
}

int gw_cloud_publish_telemetry(gw_cloud_client_t *client, const uint8_t *payload, size_t payload_len)
{
    if (client == NULL || !client->connected) {
        return -ENOTCONN;
    }

    if (payload == NULL && payload_len > 0U) {
        return -EINVAL;
    }

    return 0;
}

int gw_cloud_pump(gw_cloud_client_t *client)
{
    if (client == NULL || !client->connected) {
        return -ENOTCONN;
    }

    return 0;
}

int gw_cloud_disconnect(gw_cloud_client_t *client)
{
    if (client == NULL || !client->initialized) {
        return -EINVAL;
    }

    client->connected = false;
    return 0;
}
