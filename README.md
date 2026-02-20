# EasySmart IIoT Gateway Engine

Repositorio limpo para o motor de gateway reutilizavel em Zephyr.

Escopo atual:
- modulo `gateway_engine` em C (sem app fixo)
- tres perfis de produto: `iiot_gateway`, `generic_gateway`, `lighting_gateway`
- camada de transporte com `SPI`, `UART` e `INTERNAL` (hibrido no mesmo SoC)
- portas reais Zephyr para `SPI` e `UART`
- conector cloud Zephyr com bootstrap/secret HTTP + MQTT sobre WSS

Principio de arquitetura:
- edge core continua responsavel por determinismo
- gateway engine cuida de conectividade, integracao cloud e OTA
- o mesmo motor pode ser chamado por produtos diferentes

## Estrutura

- `gateway_engine/`: modulo Zephyr com API publica e fontes
- `docs/`: decisoes arquiteturais e guias de integracao
- `zephyr/module.yml`: registro do modulo para build do Zephyr

## Ambiente local (padrao)

Use o script abaixo antes de build/flash:

```bash
source scripts/zephyr_env.sh
```

No Windows (PowerShell):

```powershell
. .\scripts\zephyr_env.ps1
```

Padrao configurado:
- `ZEPHYR_BASE=/home/rodrigo/zephyrproject/zephyr`
- `ZEPHYR_SDK_INSTALL_DIR=/home/rodrigo/zephyr-sdk-0.17.3`
- `CMAKE_BUILD_PARALLEL_LEVEL=1`

## Integracao rapida

1. Adicione este repo no workspace/west e garanta que `zephyr/module.yml` seja detectado.
2. No seu app Zephyr, habilite no `prj.conf`:
   - `CONFIG_GW_ENGINE=y`
   - `CONFIG_GW_ENGINE_PORTS_ZEPHYR=y`
   - `CONFIG_GW_ENGINE_CLOUD_ZEPHYR=y`
   - `CONFIG_GW_ENGINE_TRANSPORT_SPI=y` ou `CONFIG_GW_ENGINE_TRANSPORT_UART=y`
3. Inclua `gateway_engine/gw_engine.h` e inicialize o motor com o perfil desejado.

Detalhes completos em `docs/INTEGRATION.md`.
