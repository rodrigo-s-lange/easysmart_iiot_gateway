#include <errno.h>
#include <stddef.h>

#include <gateway_engine/gw_transport.h>
#include <gateway_engine/ports/gw_port_uart.h>

static int uart_open(gw_transport_t *transport)
{
    gw_transport_uart_t *backend;

    if (transport == NULL || transport->ctx == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_uart_t *)transport->ctx;
    if (backend->is_open) {
        return 0;
    }

    if (backend->config.mtu == 0U) {
        backend->config.mtu = GW_TRANSPORT_DEFAULT_MTU;
    }

    if (gw_port_uart_open(&backend->config) != 0) {
        return -EIO;
    }

    backend->is_open = true;
    return 0;
}

static int uart_close(gw_transport_t *transport)
{
    gw_transport_uart_t *backend;

    if (transport == NULL || transport->ctx == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_uart_t *)transport->ctx;
    if (!backend->is_open) {
        return 0;
    }

    if (gw_port_uart_close() != 0) {
        return -EIO;
    }

    backend->is_open = false;
    return 0;
}

static int uart_tx(gw_transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    gw_transport_uart_t *backend;

    if (transport == NULL || transport->ctx == NULL || data == NULL || len == 0U) {
        return -EINVAL;
    }

    backend = (gw_transport_uart_t *)transport->ctx;
    if (!backend->is_open) {
        return -ENOTCONN;
    }

    if (len > backend->config.mtu) {
        return -EMSGSIZE;
    }

    return gw_port_uart_tx(data, len, timeout_ms);
}

static int uart_rx(gw_transport_t *transport, uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    gw_transport_uart_t *backend;

    if (transport == NULL || transport->ctx == NULL || data == NULL || out_len == NULL) {
        return -EINVAL;
    }

    backend = (gw_transport_uart_t *)transport->ctx;
    if (!backend->is_open) {
        return -ENOTCONN;
    }

    if (cap < backend->config.mtu) {
        return -ENOBUFS;
    }

    return gw_port_uart_rx(data, cap, out_len, timeout_ms);
}

static const gw_transport_api_t UART_API = {
    .open = uart_open,
    .close = uart_close,
    .tx = uart_tx,
    .rx = uart_rx,
};

int gw_transport_uart_init(gw_transport_uart_t *backend, gw_transport_t *out_transport, const gw_transport_uart_config_t *cfg)
{
    if (backend == NULL || out_transport == NULL || cfg == NULL) {
        return -EINVAL;
    }

    backend->config = *cfg;
    backend->is_open = false;

    out_transport->kind = GW_TRANSPORT_KIND_UART;
    out_transport->api = &UART_API;
    out_transport->ctx = backend;

    return 0;
}

__attribute__((weak)) int gw_port_uart_open(const gw_transport_uart_config_t *cfg)
{
    (void)cfg;
    return -ENOTSUP;
}

__attribute__((weak)) int gw_port_uart_close(void)
{
    return 0;
}

__attribute__((weak)) int gw_port_uart_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    (void)data;
    (void)len;
    (void)timeout_ms;
    return -ENOTSUP;
}

__attribute__((weak)) int gw_port_uart_rx(uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    (void)data;
    (void)cap;
    (void)timeout_ms;

    if (out_len != NULL) {
        *out_len = 0U;
    }

    return -EAGAIN;
}
