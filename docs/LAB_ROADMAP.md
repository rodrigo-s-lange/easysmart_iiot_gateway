# Lab Roadmap

## Fase A: ESP32-S3 hibrido (agora)

- gateway + edge no mesmo SoC
- transporte `INTERNAL`
- foco em cenarios de lighting (scene, dimmer, heartbeat)
- cloud em modo `stub`

Entrega: `apps/hybrid_lighting_esp32s3`.

## Fase B: Manta M8P (STM32H723) como edge externo

- gateway em ESP32-S3
- edge em STM32H723 (Manta M8P)
- transporte primario `SPI` dedicado
- fallback `UART` dedicado

Objetivo tecnico:
- manter mesmo `gateway_engine`
- trocar apenas camada de transporte e protocolo de borda
- preservar separacao: determinismo no edge, comunicacao no gateway

## Fase C: cloud real com iiot_core

- habilitar `CONFIG_GW_ENGINE_CLOUD_ZEPHYR=y`
- bootstrap + secret HMAC
- MQTT/WSS com broker retornado pelo core
