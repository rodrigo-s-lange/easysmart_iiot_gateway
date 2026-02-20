# Hybrid Lighting Lab (ESP32-S3)

Laboratorio inicial para rodar `gateway_engine` e uma simulacao de edge no mesmo SoC,
usando `GW_TRANSPORT_KIND_INTERNAL`.

## Objetivo

- validar ciclo de vida do engine (`init/start/step/send`)
- validar framing binario (`gw_link_proto`) com ACK em loop local
- validar caso inicial de lighting (scene/brightness/heartbeat)
- validar status visual no WS2812 (`GPIO48`)

## WS2812 debug (GPIO48)

Com o overlay `boards/esp32s3_devkitc_procpu.overlay`:

- vermelho: sem Wi-Fi
- verde: Wi-Fi conectado
- azul: Wi-Fi + MQTT conectado

## Wi-Fi

Configure no `prj.conf`:

```ini
CONFIG_HYBRID_WIFI_SSID="SEU_SSID"
CONFIG_HYBRID_WIFI_PSK="SUA_SENHA"
```

Se `CONFIG_HYBRID_WIFI_SSID` ficar vazio, o app nao tenta conectar.

## Build (Zephyr)

Exemplo com board ESP32-S3 (ajuste para o seu board real):

```bash
source scripts/zephyr_env.sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu apps/hybrid_lighting_esp32s3 -- -DZEPHYR_EXTRA_MODULES=$PWD
west flash
west espressif monitor
```

Se seu board tiver outro nome, substitua no `-b`.

No Windows (PowerShell), use:

```powershell
. .\scripts\zephyr_env.ps1
west build -d build\hybrid_lighting_esp32s3 -p always -b esp32s3_devkitc/esp32s3/procpu apps\hybrid_lighting_esp32s3 -- -DZEPHYR_EXTRA_MODULES=C:\dev\easysmart_iiot_gateway
west flash -d build\hybrid_lighting_esp32s3 --runner esp32 --esp-device COM8 --esp-baud-rate 921600
Set-Location build\hybrid_lighting_esp32s3
west espressif monitor -p COM8 -b 115200
```

## Notas

- Este app usa `CONFIG_GW_ENGINE_CLOUD_STUB=y` para teste local sem backend real.
- O status MQTT do LED azul depende de `lab.engine.cloud.connected`.
