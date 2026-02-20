# Integration Guide

## 1. Habilitar no app Zephyr

Exemplo de `prj.conf`:

```
CONFIG_GW_ENGINE=y
CONFIG_GW_ENGINE_PORTS_ZEPHYR=y
CONFIG_GW_ENGINE_TRANSPORT_SPI=y
CONFIG_GW_ENGINE_TRANSPORT_UART=n
CONFIG_GW_ENGINE_TRANSPORT_INTERNAL=n
CONFIG_GW_ENGINE_CLOUD_ZEPHYR=y
CONFIG_GW_ENGINE_CLOUD_STUB=n
CONFIG_GW_ENGINE_OTA_STUB=y
```

## 2. Inicializar transporte e engine

```c
#include <gateway_engine/gw_engine.h>
#include <gateway_engine/gw_transport.h>

static gw_transport_spi_t spi_backend;
static gw_transport_t transport;
static gw_engine_t engine;

void start_gateway(void)
{
    gw_transport_spi_config_t spi_cfg = {
        .bus = "SPI_2",
        .frequency_hz = 12000000,
        .slave = 0,
        .mtu = 512,
    };

    gw_engine_config_t cfg = {
        .profile = GW_PROFILE_IIOT_GATEWAY,
        .device_id = "gw-001",
        .loop_period_ms = 10,
        .cloud = {
            .api_base_url = "https://core.example.com",
            .device_id = "11111111-1111-1111-1111-111111111111",
            .hardware_id = "3030F903AA1C",
            .manufacturing_key = "MANUFACTURING_MASTER_KEY",
            .tls_sec_tag = 1,
            .mqtt_keepalive_sec = 60,
        },
        .ota = {
            .chunk_size = 1024,
            .timeout_ms = 3000,
        },
    };

    gw_transport_spi_init(&spi_backend, &transport, &spi_cfg);
    gw_engine_init(&engine, &cfg, &transport);
    gw_engine_start(&engine);
}
```

## 3. Loop principal

Chamar `gw_engine_step()` de forma periodica.

## 4. Requisitos do cloud connector

- `manufacturing_key` e obrigatoria para assinar bootstrap/secret com HMAC-SHA256.
- `tls_sec_tag` deve apontar para credenciais TLS registradas em Zephyr (`tls_credential_add`).
- `api_base_url` pode substituir URLs explicitas:
  - `/api/v1/devices/bootstrap`
  - `/api/v1/devices/secret`
- Se `secret_url` nao for informado, o conector tenta derivar a partir de `bootstrap_url` trocando `/bootstrap` por `/secret`.
