#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

#include <gateway_engine/ports/gw_port_spi.h>

typedef struct {
    const struct device *dev;
    struct spi_config cfg;
    uint16_t mtu;
    bool is_open;
} gw_spi_port_ctx_t;

static gw_spi_port_ctx_t g_spi;

int gw_port_spi_open(const gw_transport_spi_config_t *cfg)
{
    if (cfg == NULL || cfg->bus == NULL) {
        return -EINVAL;
    }

    (void)memset(&g_spi, 0, sizeof(g_spi));

    g_spi.dev = device_get_binding(cfg->bus);
    if (g_spi.dev == NULL || !device_is_ready(g_spi.dev)) {
        return -ENODEV;
    }

    g_spi.cfg.frequency = (cfg->frequency_hz == 0U) ? 1000000U : cfg->frequency_hz;
    g_spi.cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
    g_spi.cfg.slave = cfg->slave;
    g_spi.mtu = (cfg->mtu == 0U) ? GW_TRANSPORT_DEFAULT_MTU : cfg->mtu;
    g_spi.is_open = true;

    return 0;
}

int gw_port_spi_close(void)
{
    (void)memset(&g_spi, 0, sizeof(g_spi));
    return 0;
}

int gw_port_spi_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    struct spi_buf tx_buf;
    struct spi_buf_set tx;

    (void)timeout_ms;

    if (!g_spi.is_open || g_spi.dev == NULL) {
        return -ENOTCONN;
    }

    if (data == NULL || len == 0U) {
        return -EINVAL;
    }

    if (len > g_spi.mtu) {
        return -EMSGSIZE;
    }

    tx_buf.buf = (void *)data;
    tx_buf.len = len;
    tx.buffers = &tx_buf;
    tx.count = 1U;

    return spi_write(g_spi.dev, &g_spi.cfg, &tx);
}

int gw_port_spi_rx(uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms)
{
    struct spi_buf rx_buf;
    struct spi_buf_set rx;
    size_t want_len;
    int rc;

    (void)timeout_ms;

    if (out_len != NULL) {
        *out_len = 0U;
    }

    if (!g_spi.is_open || g_spi.dev == NULL) {
        return -ENOTCONN;
    }

    if (data == NULL || out_len == NULL || cap == 0U) {
        return -EINVAL;
    }

    want_len = g_spi.mtu;
    if (want_len > cap) {
        want_len = cap;
    }

    rx_buf.buf = data;
    rx_buf.len = want_len;
    rx.buffers = &rx_buf;
    rx.count = 1U;

    rc = spi_read(g_spi.dev, &g_spi.cfg, &rx);
    if (rc != 0) {
        return rc;
    }

    *out_len = want_len;
    return 0;
}
