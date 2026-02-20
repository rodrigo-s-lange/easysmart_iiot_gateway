# Transport Decision

## Padrao

1. `SPI` dedicada para `gateway <-> edge` em placas proximas (ate poucos cm).
2. `UART` dedicada como fallback e compatibilidade.
3. `INTERNAL` para modo hibrido no mesmo SoC (sem barramento fisico externo).

## Sobre JTAG como transporte

JTAG e interface de debug/programacao, nao foi desenhada como canal de dados
operacional de producao. Para um unico cabo USB-C, a abordagem recomendada e:

- USB CDC ACM para canal de dados serial, ou
- UART sobre USB bridge dedicada.

JTAG pode permanecer para debug/manutencao.
