#include <errno.h>
#include <stddef.h>

#include <gateway_engine/gw_transport.h>
#include <gateway_engine/ports/gw_port_spi.h>

static int spi_open(gw_transport_t *transport)
{
    gw_transport_spi_t *backend;

    if (transport == NULL || transport->ctx == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_spi_t *)transport->ctx;
    if (backend->is_open) {
        return 0;
    }

    if (backend->config.mtu == 0U) {
        backend->config.mtu = GW_TRANSPORT_DEFAULT_MTU;
    }

    if (gw_port_spi_open(&backend->config) != 0) {
        return -EIO;
    }

    backend->is_open = true;
    return 0;
}

static int spi_close(gw_transport_t *transport)
{
    gw_transport_spi_t *backend;

    if (transport == NULL || transport->ctx == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_spi_t *)transport->ctx;
    if (!backend->is_open) {
        return 0;
    }

    if (gw_port_spi_close() != 0) {
        return -EIO;
    }

    backend->is_open = false;
    return 0;
}

static int spi_tx(gw_transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    gw_transport_spi_t *backend;

    if (transport == NULL || transport->ctx == NULL || data == NULL || len == 0U) {
        return -EINVAL;
    }

    backend = (gw_transport_spi_t *)transport->ctx;
    if (!backend->is_open) {
        return -ENOTCONN;
    }

    if (len > backend->config.mtu) {
        return -EMSGSIZE;
    }

    return gw_port_spi_tx(data, len, timeout_ms);
}

static int spi_rx(gw_transport_t *transport, uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    gw_transport_spi_t *backend;

    if (transport == NULL || transport->ctx == NULL || data == NULL || out_len == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_spi_t *)transport->ctx;
    if (!backend->is_open) {
        return -ENOTCONN;
    }

    if (cap < backend->config.mtu) {
        return -ENOBUFS;
    }

    return gw_port_spi_rx(data, cap, out_len, timeout_ms);
}

static const gw_transport_api_t SPI_API = {
    .open = spi_open,
    .close = spi_close,
    .tx = spi_tx,
    .rx = spi_rx,
};

int gw_transport_spi_init(gw_transport_spi_t *backend, gw_transport_t *out_transport, const gw_transport_spi_config_t *cfg)
{
    if (backend == NULL || out_transport == NULL || cfg == NULL) {
        return -EINVAL;
    }

    backend->config = *cfg;
    backend->is_open = false;

    out_transport->kind = GW_TRANSPORT_KIND_SPI;
    out_transport->api = &SPI_API;
    out_transport->ctx = backend;

    return 0;
}

__attribute__((weak)) int gw_port_spi_open(const gw_transport_spi_config_t *cfg)
{
    (void)cfg;
    return -ENOTSUP;
}

__attribute__((weak)) int gw_port_spi_close(void)
{
    return 0;
}

__attribute__((weak)) int gw_port_spi_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    (void)data;
    (void)len;
    (void)timeout_ms;
    return -ENOTSUP;
}

__attribute__((weak)) int gw_port_spi_rx(uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    (void)data;
    (void)cap;
    (void)timeout_ms;

    if (out_len != NULL) {
        *out_len = 0U;
    }

    return -EAGAIN;
}
