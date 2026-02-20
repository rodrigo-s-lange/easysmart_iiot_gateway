# Gateway Engine Architecture

## Objetivo

Fornecer um motor unico de conectividade para varias familias de produto,
sem misturar determinismo do edge com conectividade IT/cloud.

## Perfis

Os nomes oficiais dos cenarios sao:
- `iiot_gateway`
- `generic_gateway`
- `lighting_gateway`

Esses perfis sao metadados de comportamento e roteamento, nao forks do codigo.

## Blocos do modulo

- `gw_engine`: orquestracao de ciclo de vida
- `transport`: SPI, UART, INTERNAL
- `link_protocol`: frame binario com CRC16 e sequencia
- `cloud`: stub da integracao com `iiot_core` (bootstrap + MQTT/WSS)
- `ota`: stub para orquestracao de atualizacao por chunks

## Regra de autoridade

- Edge/Core governa processo e determinismo
- Gateway governa comunicacao e distribuicao de atualizacao
- Falha do gateway nao deve quebrar controle local do edge
