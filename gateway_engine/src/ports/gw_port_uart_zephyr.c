#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include <gateway_engine/ports/gw_port_uart.h>

#define GW_UART_INTERCHAR_GAP_MS 2U

typedef struct {
    const struct device *dev;
    uint16_t mtu;
    bool is_open;
} gw_uart_port_ctx_t;

static gw_uart_port_ctx_t g_uart;

int gw_port_uart_open(const gw_transport_uart_config_t *cfg)
{
    struct uart_config uart_cfg;
    int rc;

    if (cfg == NULL || cfg->device == NULL) {
        return -EINVAL;
    }

    (void)memset(&g_uart, 0, sizeof(g_uart));

    g_uart.dev = device_get_binding(cfg->device);
    if (g_uart.dev == NULL || !device_is_ready(g_uart.dev)) {
        return -ENODEV;
    }

    if (cfg->baudrate > 0U) {
        uart_cfg.baudrate = cfg->baudrate;
        uart_cfg.parity = UART_CFG_PARITY_NONE;
        uart_cfg.stop_bits = UART_CFG_STOP_BITS_1;
        uart_cfg.data_bits = UART_CFG_DATA_BITS_8;
        uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;

        rc = uart_configure(g_uart.dev, &uart_cfg);
        if (rc != 0 && rc != -ENOSYS && rc != -ENOTSUP) {
            return rc;
        }
    }

    g_uart.mtu = (cfg->mtu == 0U) ? GW_TRANSPORT_DEFAULT_MTU : cfg->mtu;
    g_uart.is_open = true;

    return 0;
}

int gw_port_uart_close(void)
{
    (void)memset(&g_uart, 0, sizeof(g_uart));
    return 0;
}

int gw_port_uart_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    size_t i;

    (void)timeout_ms;

    if (!g_uart.is_open || g_uart.dev == NULL) {
        return -ENOTCONN;
    }

    if (data == NULL || len == 0U) {
        return -EINVAL;
    }

    if (len > g_uart.mtu) {
        return -EMSGSIZE;
    }

    for (i = 0; i < len; ++i) {
        uart_poll_out(g_uart.dev, data[i]);
    }

    return 0;
}

int gw_port_uart_rx(uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    int64_t start_ms;
    int64_t last_rx_ms;
    size_t len = 0U;

    if (out_len != NULL) {
        *out_len = 0U;
    }

    if (!g_uart.is_open || g_uart.dev == NULL) {
        return -ENOTCONN;
    }

    if (data == NULL || out_len == NULL || cap == 0U) {
        return -EINVAL;
    }

    if (cap > g_uart.mtu) {
        cap = g_uart.mtu;
    }

    start_ms = k_uptime_get();
    last_rx_ms = start_ms;

    while (len < cap) {
        unsigned char ch;
        int rc = uart_poll_in(g_uart.dev, &ch);

        if (rc == 0) {
            data[len++] = (uint8_t)ch;
            last_rx_ms = k_uptime_get();
            continue;
        }

        if (rc != -1) {
            return rc;
        }

        if (len > 0U) {
            if ((k_uptime_get() - last_rx_ms) >= GW_UART_INTERCHAR_GAP_MS) {
                break;
            }
        } else {
            if (timeout_ms == 0U) {
                return -EAGAIN;
            }
            if ((k_uptime_get() - start_ms) >= timeout_ms) {
                return -EAGAIN;
            }
        }

        k_sleep(K_MSEC(1));
    }

    *out_len = len;
    return (len > 0U) ? 0 : -EAGAIN;
}
