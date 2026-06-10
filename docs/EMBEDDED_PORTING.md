# Embedded Porting Guide

Este guia substitui notas específicas por um contrato geral para portar Bubble Cloud a plataformas embarcadas, mantendo a porta ESP32 em `platform/esp32/` como referência.

## Arquitetura de porta

```text
Controls / codec / storage
        |
        v
UI/Platform -> bubble_engine API -> macro map -> DSP config -> process
        ^                                                   |
        |                                                   v
        +---------- metrics/status outside audio <----------+
```

A camada embarcada deve ser fina: inicializa hardware, aloca buffers, converte controles em parâmetros e chama `bubble_engine_process` no callback/tarefa de áudio.

## Contrato de tempo real embarcado

No contexto de áudio:

- Sem alocação dinâmica.
- Sem I/O de flash, NVS, SD, UART, console, I2C de controle ou rede.
- Sem locks, mutexes, semáforos bloqueantes ou chamadas que aguardem outra tarefa.
- Sem atualização direta de display/LEDs lentos.
- Memória caller-owned: o delay buffer deve ser alocado pelo firmware antes de iniciar áudio e passado para `bubble_engine_init`.
- Determinismo por seed: presets salvos devem persistir `rng_seed` quando a intenção for render reproduzível.

Use filas lock-free, double buffering ou snapshots atômicos para transferir controles da UI/tarefa lenta para a tarefa de áudio. Aplique mudanças entre blocos.

## Orçamento de áudio

| Item | Valor de referência | Notas |
| --- | ---: | --- |
| Sample rate | 44,100 Hz | Igual a `BUBBLES_SAMPLE_RATE`; outra taxa exige resampler explícito. |
| Bloco interno | 32 frames | Igual a `BUBBLES_BLOCK_SIZE`; cerca de 0,73 ms a 44,1 kHz. |
| Entrada do core | mono float | Some/converta entradas de codec antes do core. |
| Saída do core | estéreo float | Converta para formato do codec depois do core. |
| Delay buffer | 88.200 amostras `int16_t` | Caller-owned; 2 segundos a 44,1 kHz. |

## Seleção de perfil

| Target | Perfil inicial recomendado | Observação |
| --- | --- | --- |
| MCU pequeno/sem FPU forte | `MCU_SAFE` | Prioriza estabilidade e menor limite de vozes. |
| MCU com mais RAM/CPU | `MCU_PLUS` | Melhor densidade mantendo orçamento embarcado. |
| Linux embarcado/WebView | `WEB_STANDARD` | Use se medições confirmarem headroom. |
| Render offline no dispositivo | `WEB_ULTRA` | Apenas fora de deadlines rígidos ou com CPU suficiente. |

Perfis podem mudar limite de vozes, CPU/RAM, densidade percebida e ocorrência de voice stealing. Eles não podem exigir alocação no áudio, mudar seed, alterar formato de preset ou redefinir macros.

## Checklist de porting

1. Confirme sample rate real do codec e implemente resampling se não for 44,1 kHz.
2. Aloque `BubbleEngine_t`, delay buffer e buffers de bloco antes de iniciar áudio.
3. Chame `bubble_engine_default_config`, escolha perfil e inicialize com `bubble_engine_init`.
4. Garanta que o callback de áudio apenas converta buffers, aplique snapshots de controles e processe.
5. Mova armazenamento de presets, display, logs e telemetria para tarefas fora do áudio.
6. Meça tempo máximo de bloco com presets densos e freeze/motion ativos.
7. Teste boot, troca de preset, bypass, clipping, corrupção de storage e queda de energia.

## Porta ESP32 de referência

A referência em `platform/esp32/` usa I2S para codec, NVS para presets e OLED para status. Esses módulos são exemplos de hardware layer; nenhum deles deve ser chamado pelo core DSP. Ao adaptar para uma placa real, substitua mapas de pinos, sequência de codec, política de botões/ADC e layout de display conforme o hardware final.
