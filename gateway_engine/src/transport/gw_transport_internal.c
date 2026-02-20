#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <gateway_engine/gw_transport.h>

static int internal_open(gw_transport_t *transport)
{
    gw_transport_internal_t *backend;

    if (transport == NULL || transport->ctx == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_internal_t *)transport->ctx;
    backend->is_open = true;
    backend->rx_len = 0U;
    backend->rx_pending = false;

    if (backend->config.mtu == 0U) {
        backend->config.mtu = GW_TRANSPORT_DEFAULT_MTU;
    }

    return 0;
}

static int internal_close(gw_transport_t *transport)
{
    gw_transport_internal_t *backend;

    if (transport == NULL || transport->ctx == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_internal_t *)transport->ctx;
    backend->is_open = false;
    backend->rx_len = 0U;
    backend->rx_pending = false;

    return 0;
}

static int internal_tx(gw_transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    gw_transport_internal_t *backend;
    size_t rx_len = 0U;
    int rc;

    (void)timeout_ms;

    if (transport == NULL || transport->ctx == NULL || data == NULL || len == 0U) {
        return -EINVAL;
    }

    backend = (gw_transport_internal_t *)transport->ctx;
    if (!backend->is_open) {
        return -ENOTCONN;
    }

    if (len > backend->config.mtu) {
        return -EMSGSIZE;
    }

    if (backend->config.exchange_cb == NULL) {
        return 0;
    }

    rc = backend->config.exchange_cb(
        data,
        len,
        backend->rx_staging,
        sizeof(backend->rx_staging),
        &rx_len,
        backend->config.user_data);
    if (rc != 0) {
        return rc;
    }

    if (rx_len > sizeof(backend->rx_staging)) {
        return -EMSGSIZE;
    }

    backend->rx_len = rx_len;
    backend->rx_pending = (rx_len > 0U);

    return 0;
}

static int internal_rx(gw_transport_t *transport, uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    gw_transport_internal_t *backend;

    (void)timeout_ms;

    if (transport == NULL || transport->ctx == NULL || data == NULL || out_len == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_internal_t *)transport->ctx;
    if (!backend->is_open) {
        return -ENOTCONN;
    }

    if (!backend->rx_pending) {
        *out_len = 0U;
        return -EAGAIN;
    }

    if (cap < backend->rx_len) {
        return -ENOBUFS;
    }

    (void)memcpy(data, backend->rx_staging, backend->rx_len);
    *out_len = backend->rx_len;
    backend->rx_pending = false;
    backend->rx_len = 0U;

    return 0;
}

static const gw_transport_api_t INTERNAL_API = {
    .open = internal_open,
    .close = internal_close,
    .tx = internal_tx,
    .rx = internal_rx,
};

int gw_transport_internal_init(
    gw_transport_internal_t *backend,
    gw_transport_t *out_transport,
    const gw_transport_internal_config_t *cfg)
{
    if (backend == NULL || out_transport == NULL || cfg == NULL) {
        return -EINVAL;
    }

    backend->config = *cfg;
    backend->is_open = false;
    backend->rx_len = 0U;
    backend->rx_pending = false;

    out_transport->kind = GW_TRANSPORT_KIND_INTERNAL;
    out_transport->api = &INTERNAL_API;
    out_transport->ctx = backend;

    return 0;
}
