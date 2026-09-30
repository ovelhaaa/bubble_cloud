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
| Delay buffer | 88.200 amostras `BubbleRingSample_t` | Caller-owned; 2 segundos a 44,1 kHz (172,3 KiB em `int16_t`). |

## Ring Buffer Storage Backend & Memória (`BUBBLES_RING_FLOAT`)

A partir da milestone M4C, o armazenamento do ring buffer é configurado em tempo de compilação:

- **Alvos Embarcados (`MCU_SAFE`, `MCU_PLUS`, ESP32)**:
  - Definem `BUBBLES_RING_FLOAT=0` (padrão automático em `BUBBLES_TARGET_MCU` ou `ESP_PLATFORM`).
  - O tipo `BubbleRingSample_t` é `int16_t` (2 bytes por amostra).
  - Escrita com TPDF dither (~1 LSB) via gerador PRNG dedicado e desacoplado (`ring_dither_rng`), eliminando distorção de truncamento em caudas baixas sem afetar o determinismo musical.
- **Alvos Desktop / WASM**:
  - Definem `BUBBLES_RING_FLOAT=1`.
  - O tipo `BubbleRingSample_t` é `float` (4 bytes por amostra).

### Distinção Crítica: Samples vs Bytes

Para alocação correta de memória caller-owned pelo firmware:

- `SoundBubbles_RequiredBufferSamples(sample_rate)`: retorna a contagem de **amostras** (ex: 88.200 amostras para 2 s a 44,1 kHz).
- `SoundBubbles_RequiredBufferBytes(sample_rate)`: retorna o tamanho exato em **bytes** (`samples * sizeof(BubbleRingSample_t)`).

> [!WARNING]
> Nunca assuma `bytes == samples * 2` ou `bytes == samples * 4` hardcoded no firmware. Utilize sempre `SoundBubbles_RequiredBufferBytes(sample_rate)` ou `bubble_engine_required_buffer_bytes(sample_rate)` para alocar o buffer estático/DMA.

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
