#ifndef GW_TRANSPORT_H
#define GW_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GW_TRANSPORT_DEFAULT_MTU 512U
#define GW_TRANSPORT_INTERNAL_RX_MAX 1024U

typedef enum {
    GW_TRANSPORT_KIND_SPI = 0,
    GW_TRANSPORT_KIND_UART = 1,
    GW_TRANSPORT_KIND_INTERNAL = 2,
} gw_transport_kind_t;

struct gw_transport;
typedef struct gw_transport gw_transport_t;

typedef struct {
    int (*open)(gw_transport_t *transport);
    int (*close)(gw_transport_t *transport);
    int (*tx)(gw_transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms);
    int (*rx)(gw_transport_t *transport, uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms);
} gw_transport_api_t;

struct gw_transport {
    gw_transport_kind_t kind;
    const gw_transport_api_t *api;
    void *ctx;
};

typedef struct {
    const char *bus;
    uint32_t frequency_hz;
    uint16_t slave;
    uint16_t mtu;
} gw_transport_spi_config_t;

typedef struct {
    const char *device;
    uint32_t baudrate;
    uint16_t mtu;
} gw_transport_uart_config_t;

typedef int (*gw_internal_exchange_fn)(
    const uint8_t *tx_data,
    size_t tx_len,
    uint8_t *rx_data,
    size_t rx_cap,
    size_t *rx_len,
    void *user_data);

typedef struct {
    gw_internal_exchange_fn exchange_cb;
    void *user_data;
    uint16_t mtu;
} gw_transport_internal_config_t;

typedef struct {
    gw_transport_spi_config_t config;
    bool is_open;
} gw_transport_spi_t;

typedef struct {
    gw_transport_uart_config_t config;
    bool is_open;
} gw_transport_uart_t;

typedef struct {
    gw_transport_internal_config_t config;
    bool is_open;
    uint8_t rx_staging[GW_TRANSPORT_INTERNAL_RX_MAX];
    size_t rx_len;
    bool rx_pending;
} gw_transport_internal_t;

int gw_transport_open(gw_transport_t *transport);
int gw_transport_close(gw_transport_t *transport);
int gw_transport_tx(gw_transport_t *transport, const uint8_t *data, size_t len, uint32_t timeout_ms);
int gw_transport_rx(gw_transport_t *transport, uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms);

int gw_transport_spi_init(gw_transport_spi_t *backend, gw_transport_t *out_transport, const gw_transport_spi_config_t *cfg);
int gw_transport_uart_init(gw_transport_uart_t *backend, gw_transport_t *out_transport, const gw_transport_uart_config_t *cfg);
int gw_transport_internal_init(
    gw_transport_internal_t *backend,
    gw_transport_t *out_transport,
    const gw_transport_internal_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
