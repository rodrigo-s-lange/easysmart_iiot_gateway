#include <errno.h>
#include <stddef.h>

#include <gateway_engine/gw_transport.h>

int gw_transport_open(gw_transport_t *transport)
{
    if (transport == NULL || transport->api == NULL || transport->api->open == NULL) {
        return -EINVAL;
    }

    return transport->api->open(transport);
}

int gw_transport_close(gw_transport_t *transport)
{
    if (transport == NULL || transport->api == NULL || transport->api->close == NULL) {
        return -EINVAL;
    }

    return transport->api->close(transport);
}

int gw_transport_tx(gw_transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (transport == NULL || transport->api == NULL || transport->api->tx == NULL) {
        return -EINVAL;
    }

    return transport->api->tx(transport, data, len, timeout_ms);
}

int gw_transport_rx(gw_transport_t *transport, uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    if (transport == NULL || transport->api == NULL || transport->api->rx == NULL) {
        return -EINVAL;
    }

    return transport->api->rx(transport, data, cap, out_len, timeout_ms);
}
