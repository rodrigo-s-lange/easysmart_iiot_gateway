#ifndef GW_PORT_SPI_H
#define GW_PORT_SPI_H

#include <stddef.h>
#include <stdint.h>

#include <gateway_engine/gw_transport.h>

#ifdef __cplusplus
extern "C" {
#endif

int gw_port_spi_open(const gw_transport_spi_config_t *cfg);
int gw_port_spi_close(void);
int gw_port_spi_tx(const uint8_t *data, size_t len, uint32_t timeout_ms);
int gw_port_spi_rx(uint8_t *data, size_t cap, size_t *out_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
