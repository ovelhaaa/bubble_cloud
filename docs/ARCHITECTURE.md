# Bubble Cloud Architecture

## Camadas

```text
+-------------+     +-------------------+     +-----------+     +------------+     +---------+
| UI/Platform | --> | bubble_engine API | --> | macro map | --> | DSP config | --> | process |
+-------------+     +-------------------+     +-----------+     +------------+     +---------+
```

### 1. UI/Platform

Hosts incluem:

- UI web e AudioWorklet em `ui/web/`.
- Wrappers WASM em `platform/wasm/`.
- Renderizador offline em `platform/offline/`.
- Porta ESP32 de referência em `platform/esp32/`.

Essa camada é responsável por arquivos, UI, MIDI/controles, armazenamento, threads, filas e buffers de áudio externos. Ela não deve introduzir dependências no core DSP.

### 2. `bubble_engine` API

A API pública C fica em `core/engine/bubble_engine.h`. Ela expõe:

- Inicialização/reset do estado do motor.
- Processamento de áudio mono para saída estéreo.
- Set/get de parâmetros públicos.
- Load/save de `BubbleEnginePreset_t`.
- Seleção de `BubbleQualityProfile`.
- Callback opcional de métricas por bloco.

A API é a única fronteira que plataformas novas devem chamar diretamente. Símbolos `SoundBubbles_*` existem como camada DSP interna/compatibilidade e não devem ser a primeira escolha de novos hosts.

### 3. Macro map

O macro map traduz controles musicais normalizados (`density`, `bloom`, `motion`, `texture`, `space`, `gravity`, `memory`, `clarity`, `freeze`, `sparkle`, `warmth`, `mix`) para parâmetros brutos de DSP. O objetivo é manter UI e presets em linguagem musical, enquanto a implementação pode ajustar detalhes internos com validação sonora.

### 4. DSP config

`EngineConfig_t` concentra thresholds, densidades, regiões de leitura, pitch/reverse/freeze, difusão, limiter, motion, ritmo, perfil de qualidade e limite ativo de vozes. Configurações vindas de presets ou macros devem ser clampadas/normalizadas antes de chegar ao hot path.

### 5. Process

`bubble_engine_process(engine, in_mono, out_left, out_right, num_samples)` é o caminho de áudio. Ele consome amostras mono, atualiza o buffer de delay caller-owned e renderiza saída estéreo. O design assume blocos pequenos; `BUBBLES_BLOCK_SIZE` é o quantum de controle interno.

### 6. Ring Storage Backend Specialization (M4C)

O armazenamento do ring buffer granular é desacoplado do processamento do DSP e especializado em tempo de compilação:

```text
JUCE / VST / Desktop / WebAudio (WASM)
  └─► BUBBLES_RING_FLOAT=1  ──►  BubbleRingSample_t = float (32-bit IEEE 754)

Embedded MCU (ESP32, ARM Cortex-M)
  └─► BUBBLES_RING_FLOAT=0  ──►  BubbleRingSample_t = int16_t (Q15 saturado + TPDF Dither)
```

- **Desacoplamento do Perfil de Qualidade**: `QUALITY_PROFILE` (`MCU_SAFE`, `MCU_PLUS`, `WEB_STANDARD`, `WEB_ULTRA`) controla dinamicamente limite de vozes, tipo de interpolação (linear vs Hermite) e orçamentos de grãos. O formato de armazenamento do ring (`BUBBLES_RING_FLOAT`) é estritamente uma propriedade da build/alvo e **nunca** muda por preset ou troca de cena.
- **Abstração Centralizada**: O acesso ao buffer de delay ocorre unicamente através de helpers inlined (`Ring_ReadNormalizedSample`, `Ring_ReadSample`, `Ring_WriteSample`, `Ring_ClearBuffer`), evitando bifurcações condicionais no hot path DSP.
- **API de Memória Explícita**:
  - `SoundBubbles_RequiredBufferSamples(sr)` / `bubble_engine_required_buffer_samples(sr)`: retorna a contagem de amostras.
  - `SoundBubbles_RequiredBufferBytes(sr)` / `bubble_engine_required_buffer_bytes(sr)`: retorna a contagem exata de bytes (`samples * sizeof(BubbleRingSample_t)`).

## Contrato de tempo real

Dentro do caminho de áudio (`process` e funções chamadas por ele):

- **Sem alocação**: nada de `malloc`, `free`, containers dinâmicos ou crescimento implícito.
- **Sem I/O**: nada de arquivo, console, rede, flash, NVS, DOM, Web APIs ou logs síncronos.
- **Sem locks**: nada de mutex, semáforo, espera ativa por outro thread ou chamada que possa bloquear.
- **Memória caller-owned**: buffers de entrada/saída e delay são fornecidos pelo host; o motor usa estado fixo dentro de `BubbleEngine_t`.
- **Determinismo por seed**: mesma seed, mesmas amostras de entrada, mesma sequência de parâmetros/configuração e mesmo perfil devem produzir as mesmas decisões pseudoaleatórias e resultado reprodutível dentro do mesmo backend numérico.

Fora do áudio, hosts podem usar filas, locks, armazenamento e alocação, desde que convertam mudanças em snapshots/valores aplicáveis entre blocos.

## Fluxo de atualização de parâmetros

```text
UI gesture / preset load
        |
        v
bubble_engine_set_parameter / bubble_engine_load_preset
        |
        v
macro targets + dirty mask
        |
        v
control-rate smoothing / macro resolve
        |
        v
runtime DSP config snapshot
        |
        v
bubble_engine_process
```

A suavização dos macros usa uma constante de tempo em segundos e deriva o coeficiente do sample rate atual. Assim, automação e morph mantêm praticamente a mesma velocidade perceptiva em 44,1, 48, 88,2 e 96 kHz. No wrapper JUCE, callbacks de parâmetro atualizam apenas estado atômico; a aplicação no engine ocorre no início do bloco de áudio, usando cache fixo e sem containers que possam alocar.

O morph de cenas também vive nessa fronteira de plataforma: Density interpola em domínio logarítmico, Mix em potência constante, Space usa uma curva cossenoidal e os demais macros usam smoothstep. Parâmetros discretos e Freeze têm uma banda de histerese de 45–55% para não oscilar quando a automação permanece próxima do centro.

## Separação de responsabilidades

| Responsabilidade | Core engine | Plataforma |
| --- | --- | --- |
| Estado DSP e vozes | Sim | Não |
| Delay buffer | Usa | Aloca/possui |
| Audio callback | Processa | Agenda e fornece buffers |
| Preset JSON/UI | Não | Sim |
| Persistência | Não | Sim |
| Codec/I2S/WebAudio | Não | Sim |
| Perfis de qualidade | Define limites | Escolhe perfil adequado |
