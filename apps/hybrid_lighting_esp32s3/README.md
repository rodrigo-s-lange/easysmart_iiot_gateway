# Hybrid Lighting Lab (ESP32-S3)

Laboratorio inicial para rodar `gateway_engine` e uma simulacao de edge no mesmo SoC,
usando `GW_TRANSPORT_KIND_INTERNAL`.

## Objetivo

- validar ciclo de vida do engine (`init/start/step/send`)
- validar framing binario (`gw_link_proto`) com ACK em loop local
- validar caso inicial de lighting (scene/brightness/heartbeat)

## Build (Zephyr)

Exemplo com board ESP32-S3 (ajuste para o seu board real):

```bash
source scripts/zephyr_env.sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu apps/hybrid_lighting_esp32s3
west flash
west espressif monitor
```

Se seu board tiver outro nome, substitua no `-b`.

No Windows (PowerShell), use:

```powershell
. .\scripts\zephyr_env.ps1
west build -d build\hybrid_lighting_esp32s3 -p always -b esp32s3_devkitc/esp32s3/procpu apps/hybrid_lighting_esp32s3 -- -DZEPHYR_EXTRA_MODULES=$PWD
west flash -d build\hybrid_lighting_esp32s3 --runner esp32 --esp-device COM5 --esp-baud-rate 921600
Set-Location build\hybrid_lighting_esp32s3
west espressif monitor -p COM5 -b 115200
```

## Notas

- Este app usa `CONFIG_GW_ENGINE_CLOUD_STUB=y` para teste local sem rede.
- O proximo passo e trocar para `CONFIG_GW_ENGINE_CLOUD_ZEPHYR=y` e apontar para `iiot_core`.
