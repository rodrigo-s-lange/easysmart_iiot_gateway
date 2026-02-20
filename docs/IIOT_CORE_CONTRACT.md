# IIoT Core Contract Notes

Cloud connector Zephyr implementado para contrato com `iiot_core`:

- bootstrap: `POST /api/v1/devices/bootstrap` com assinatura HMAC
- segredo: `POST /api/v1/devices/secret` com assinatura HMAC
- broker MQTT via `wss://.../mqtt`
- publish em `topic_prefix + /slot/{n}` (default `slot/0`)

Fluxo atual do conector:
1. `bootstrap` para status do dispositivo (`claimed`/`active` etc).
2. `secret` para credenciais MQTT quando necessario.
3. conexao MQTT/WSS e loop com `mqtt_input` + `mqtt_live`.
