# Embedded DSP Architecture: "Sound Bubbles" Granular Engine (ESP32-S3)

## 1. Implementation Summary
A engine opera como um processador DSP em blocos de 32 amostras (`CONTROL_RATE_SAMPLES`) a 44.1 kHz. O processamento é dividido em três camadas rígidas:
*   **Audio-Rate (Per-Sample):** Escrita no buffer mono circular contínuo, leitura interpolada (linear) para cada uma das `MAX_VOICES` (12 na v1), lookup na tabela de janelas, aplicação de pan L/R, e acúmulo nos 3 busses globais estéreo.
*   **Block-Rate / Control-Rate (A cada 32 amostras = ~0.72 ms):** Atualização do envelope RMS e derivada, máquina de estados, ducking de envelope, e agendamento de spawns (Scheduler) baseado em um acumulador de densidade.
*   **Spawn-Time:** Inicialização atômica de uma struct de voz inativa ou preempção (roubo de voz com fade rápido de 1 ms).

Não há alocação dinâmica (`malloc/new` proibidos no loop de áudio). Todos os buffers, busses e pools de vozes são alocados estaticamente em SRAM/DRAM. O playback base das bolhas de corpo e sustentação é fixado em 1.0x (delta de fase = 1) para poupar CPU, restringindo jitter apenas à posição inicial de leitura.

## 2. Recommended Constants for v1

```c
// System
#define SAMPLE_RATE 44100.0f
#define CONTROL_RATE_SAMPLES 32
#define MAX_VOICES 12
#define BUFFER_SECONDS 2
#define BUFFER_SIZE (SAMPLE_RATE * BUFFER_SECONDS)

// Time constants (milliseconds)
#define MICRO_DUR_MIN 8.0f
#define MICRO_DUR_MAX 16.0f
#define SHORT_DUR_MIN 20.0f
#define SHORT_DUR_MAX 40.0f
#define BODY_DUR_MIN 40.0f
#define BODY_DUR_MAX 120.0f
#define PREEMPTION_FADE_MS 1.0f

// Analysis thresholds (Linear amplitude 0.0 - 1.0)
#define NOISE_FLOOR_THRESH 0.001f    // -60 dBFS
#define SUSTAIN_THRESH 0.05f         // -26 dBFS
#define TRACKING_THRESH 0.01f        // -40 dBFS
#define TRANSIENT_THRESH 0.08f       // Delta dE/dt para trigger

// Engine timing
#define ATTACK_BURST_DURATION_MS 50.0f
#define DUCKING_ATTACK_MS 5.0f       // Drop rápido para 0.1
#define DUCKING_RELEASE_MS 100.0f    // Ramp lenta para 1.0

// Density targets (Spawns por segundo)
#define DENSITY_BURST 50.0f
#define DENSITY_SUSTAIN_MAX 25.0f
#define DENSITY_DECAY_MIN 2.0f

// Read distance offsets (Samples relativas ao write head)
#define OFFSET_MICRO_MIN (int)(0.005f * SAMPLE_RATE) // 5ms
#define OFFSET_MICRO_MAX (int)(0.020f * SAMPLE_RATE) // 20ms
#define OFFSET_BODY_MIN  (int)(0.100f * SAMPLE_RATE) // 100ms
#define OFFSET_BODY_MAX  (int)(0.500f * SAMPLE_RATE) // 500ms
```

## 3. Core Enums and Structs

```c
typedef enum {
    STATE_SILENCE = 0,
    STATE_ATTACK_BURST,
    STATE_SUSTAIN_BODY,
    STATE_SPARSE_DECAY
} EngineState;

typedef enum {
    CLASS_MICRO = 0,
    CLASS_SHORT,
    CLASS_BODY
} BubbleClass;

typedef enum {
    VOICE_INACTIVE = 0,
    VOICE_ACTIVE,
    VOICE_PREEMPT_FADING
} VoiceState;

// LUT Types for Windowing
typedef enum {
    WINDOW_TRAPEZOIDAL = 0,
    WINDOW_HANN
} WindowType;

typedef struct {
    VoiceState state;
    BubbleClass b_class;

    float phase;          // 0.0 a 1.0, progresso da bolha
    float phase_inc;      // delta por sample = 1.0 / (duration_samples)
    float read_ptr;       // Posição de leitura float para interpolação linear
    float playback_rate;  // 1.0f default, permite jitter em micro-ataques

    float amplitude;      // Multiplicador estático (0.0 a 1.0)
    float pan_l;          // Ganho do canal L (0.0 a 1.0)
    float pan_r;          // Ganho do canal R (0.0 a 1.0)

    WindowType win_type;
    float fade_mult;      // Usado durante preempção (1.0 até 0.0)
} BubbleVoice;

typedef struct {
    EngineState state;
    float env_level;
    float env_deriv;
    bool transient_flag;
    float recent_attack_strength;

    float wet_ducking_state;
    float target_density;           // Em spawns/sec
    float spawn_accumulator;        // Usado pelo scheduler

    int write_head;
    int buffer_interest_region;     // Offset fixado no ataque para guiar o Body
    int burst_timer_samples;
} EngineGlobals;
```

## 4. Engine Update Flow

A engine atualiza na seguinte cadência:

**A Cada Amostra (`process_audio_sample()`):**
1. Grava `input` no `delay_buffer[write_head]`.
2. Avança e faz wrap do `write_head`.
3. Zera os buffers locais dos 3 busses globais (Attack, Flat, Sustain).
4. Para cada `voz` em `MAX_VOICES` onde `state != VOICE_INACTIVE`:
   - Avança `phase += phase_inc`. Se `phase >= 1.0f`, marca como `VOICE_INACTIVE`.
   - Se `state == VOICE_PREEMPT_FADING`: reduz `fade_mult` a cada sample (fade de 1ms = 44 samples). Ao chegar a 0, reinicia a voz com os novos parâmetros cacheados e muda para `VOICE_ACTIVE`.
   - Avança `read_ptr += playback_rate`. Faz wrap se sair dos limites.
   - Lê `delay_buffer` com interpolação linear em `read_ptr`.
   - Lê o valor da janela na LUT baseando-se em `phase` e `win_type`.
   - `sample = buffer_val * window_val * amplitude * fade_mult`.
   - Acumula `sample * pan_l` e `sample * pan_r` no bus correspondente a `b_class`.
5. Filtra e soma os 3 busses. Multiplica a soma estéreo pelo `wet_ducking_state`.

**A Cada 32 Amostras (`process_control_block()`):**
1. Executa envelope follower RMS no bloco atual. Calcula `env_level`.
2. Calcula `env_deriv = env_level - prev_env_level`.
3. Atualiza `transient_flag` se `env_deriv > TRANSIENT_THRESH`.
4. Suaviza `wet_ducking_state` (attack rápido de 5ms, release longo de 100ms) em direção ao target do ducking (0.1 no ataque, 1.0 caso contrário).
5. Chama `update_state_machine()`.
6. Chama `update_scheduler()`.

## 5. State Machine Rules

Em `update_state_machine()`:

*   **STATE_SILENCE:**
    *   Se `transient_flag == true` -> Vai para `STATE_ATTACK_BURST`.
    *   Mantém `target_density = 0.0f`.
*   **STATE_ATTACK_BURST:**
    *   Na entrada: Fixa `recent_attack_strength = env_deriv`, define `wet_ducking_target = 0.1f`, trava `buffer_interest_region = OFFSET_MICRO_MAX`, inicializa `burst_timer_samples = ATTACK_BURST_DURATION_MS * (SAMPLE_RATE/1000)`.
    *   Se `burst_timer_samples <= 0` AND `env_level > SUSTAIN_THRESH` -> Vai para `STATE_SUSTAIN_BODY`.
    *   Se `burst_timer_samples <= 0` AND `env_level <= SUSTAIN_THRESH` -> Vai para `STATE_SPARSE_DECAY`.
    *   Se `transient_flag == true` -> Reinicia Burst (Re-trigger).
    *   Mantém `target_density = DENSITY_BURST`.
*   **STATE_SUSTAIN_BODY:**
    *   Na entrada: `wet_ducking_target = 1.0f`.
    *   Se `transient_flag == true` -> Vai para `STATE_ATTACK_BURST`.
    *   Se `env_level < TRACKING_THRESH` -> Vai para `STATE_SPARSE_DECAY`.
    *   Faz drift lento do `buffer_interest_region` (ex: +1 sample por control tick, afastando-se do write_head).
    *   `target_density = DENSITY_SUSTAIN_MAX * (env_level / prev_peak_level)`.
*   **STATE_SPARSE_DECAY:**
    *   Se `transient_flag == true` -> Vai para `STATE_ATTACK_BURST`.
    *   Se `env_level < NOISE_FLOOR_THRESH` -> Vai para `STATE_SILENCE`.
    *   `target_density` cai exponencialmente baseada no `env_level`.

## 6. Scheduler and Spawn Logic

Em `update_scheduler()`:

A engine usa um "Acumulador de Fase de Spawn" alimentado pela `target_density` a cada bloco de controle (32 samples).
`spawn_accumulator += (target_density * CONTROL_RATE_SAMPLES) / SAMPLE_RATE;`

*   **Exceção de Transient Burst:**
    *   Se `transient_flag` ativou neste exato tick, force um loop:
        *   Tente alocar 2 a 4 vozes imediatamente.
        *   Tipo forçado: `CLASS_MICRO`.
        *   Zere o `spawn_accumulator` para evitar engasgos pós-burst.
*   **Rotina Normal:**
    *   Enquanto `spawn_accumulator >= 1.0f`:
        *   Dispare `allocate_voice()`.
        *   Reduza `spawn_accumulator -= 1.0f` (ou `1.0f + jitter_pequeno` para evitar alinhamento mecânico estrito).
        *   Selecione a classe da bolha baseada no Estado:
            *   Em Burst: 80% Micro, 20% Short.
            *   Em Sustain: 70% Body, 30% Short.
            *   Em Decay: 100% Body.

## 7. Voice Allocation and Stealing

A função `allocate_voice()` lida com a busca e preempção.
1. **Busca linear por inatividade:** Itera o array `BubbleVoice[MAX_VOICES]`. Se achar `state == VOICE_INACTIVE`, seleciona esta voz e pula para inicialização.
2. **Se cheio (Stealing):**
    *   Procura a voz de menor prioridade.
    *   A métrica de penalidade de prioridade:
        *   Se `b_class == CLASS_MICRO` E `phase < 0.5f`: Penalidade infinita (Não roube um ataque no início da vida).
        *   Se `b_class == CLASS_BODY`: Penalidade baixa. Roube o que tiver o maior `phase` (bolha mais velha e perto de morrer).
        *   Se todas são micro: Roube a que tiver o maior `phase`.
3. **Fade de Preempção:**
    *   A voz escolhida (roubada) muda para `state = VOICE_PREEMPT_FADING`.
    *   A struct recebe um ponteiro para os novos parâmetros pré-calculados. O loop de áudio per-sample se encarrega de zerar o `fade_mult` em 1ms. Ao terminar, a própria engine per-sample converte a voz para os novos dados e muda para `VOICE_ACTIVE`.

## 8. Per-Voice Audio Processing

Custos limitados para o ESP32-S3 em cada ciclo do loop de vozes:
1. `phase += phase_inc`.
2. Um IF para verificar término (`phase >= 1.0f`).
3. Dois IF-Else encadeados para a LUT de janela (Trapezoidal ou Hann). 1 leitura de float em array.
4. `read_ptr += playback_rate` (1 soma).
5. Wrap de `read_ptr` usando lógica if/else simples, evitando módulo `%` caro em float.
6. Interpolação linear de leitura:
   `int idx = (int)read_ptr; float frac = read_ptr - idx;`
   `val = buf[idx] + frac * (buf[idx+1] - buf[idx]);`
7. Multiplicação por janela, amplitude e fade: `val *= win * amp * fade`.
8. Pan: `bus_l += val * pan_l; bus_r += val * pan_r;`.

*Nenhum filtro, LFO complexo, ou envolvente per-sample além do acumulador de fade.*

## 9. Global Bus Processing

O processamento ocorre pós-soma das `MAX_VOICES`, manipulando apenas 3 busses estéreo e gerando um overhead constante independentemente do número de bolhas ativas.

*   **Attack Bus (Class 1):**
    *   Recebe hard-panned L/R outputs.
    *   Filtro: Passa-alta Global (Biquad ou 1-pole HPF, cutoff ~800 Hz).
*   **Flat Bus (Class 2):**
    *   Nenhum filtro. Mix direto na soma master.
*   **Sustain Bus (Class 3):**
    *   Filtro: Passa-baixa Global (1-pole LPF, cutoff ~2000 Hz, fixo).

*   **Mix Final:**
    *   `Wet_L = (Attack_L + Flat_L + Sustain_L) * global_wet_ducking_state * wet_volume_pot`.
    *   `Output_L = Dry_L + Wet_L`. (Sem latência no caminho do Dry).

## 10. Performance Notes for ESP32-S3

*   **FPU Limitation:** O ESP32-S3 tem suporte single-precision hardware float (FPU). Evite a todo custo variáveis `double`. Certifique-se de que constantes literais tenham o sufixo `f` (ex: `1.0f`, não `1.0`).
*   **Divisões Float:** Divisões são muito mais caras que multiplicações. Quando calcular `phase_inc`, calcule no momento do spawn (`1.0f / duration_in_samples`) e use apenas soma no loop per-sample.
*   **Módulo em Float:** Não use `fmod()`. Para fazer o wrap do `read_ptr` no buffer, use ifs (`if (read_ptr >= BUFFER_SIZE) read_ptr -= BUFFER_SIZE; if (read_ptr < 0) read_ptr += BUFFER_SIZE;`).
*   **Memory Placement:**
    *   `BubbleVoice pool[MAX_VOICES]` e as variáveis de controle DEVEM ser alocadas em IRAM (SRAM rápida).
    *   O `delay_buffer` contínuo de 2 segundos ocupa `2 * 44100 * 4 bytes = ~352 KB`. Isso deve ser alocado em PSRAM via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. Dado que o ESP32-S3 N16R8 tem PSRAM octal, a largura de banda é suficiente para acesso sequencial, mas a leitura com interpolação espalhada (12 ponteiros diferentes) irá causar miss na cache. Para mitigar, a interpolação deve permanecer estritamente linear.
*   **LUTs:** As tabelas de janela (512 ou 1024 amostras) devem estar em IRAM para garantir acesso num único ciclo de clock.

## 11. Pseudocode Skeleton

```c
// Control-rate tick (A cada 32 amostras)
void process_control_block(float* buffer, int block_size) {
    float rms = calculate_rms(buffer, block_size);
    engine.env_deriv = rms - engine.env_level;
    engine.env_level = rms;

    // Transient Detection
    engine.transient_flag = (engine.env_deriv > TRANSIENT_THRESH);
    if(engine.transient_flag) {
        engine.recent_attack_strength = engine.env_deriv;
    }

    // Ducking smooth
    float duck_target = (engine.state == STATE_ATTACK_BURST) ? 0.1f : 1.0f;
    float duck_coef = (duck_target < engine.wet_ducking_state) ?
                      duck_attack_coef : duck_release_coef;
    engine.wet_ducking_state += (duck_target - engine.wet_ducking_state) * duck_coef;

    update_state_machine();
    update_scheduler();
}

// Inicialização atômica na preempção/spawn
void spawn_bubble(BubbleClass target_class) {
    int v_idx = find_voice_to_steal();
    BubbleVoice* v = &voice_pool[v_idx];

    float duration_ms;
    if(target_class == CLASS_MICRO) {
        duration_ms = random_range(MICRO_DUR_MIN, MICRO_DUR_MAX);
        v->read_ptr = engine.write_head - random_range(OFFSET_MICRO_MIN, OFFSET_MICRO_MAX);
        v->amplitude = engine.recent_attack_strength;
        v->win_type = WINDOW_TRAPEZOIDAL;
        // ... set jitter rate, hard pan ...
    } else { /* CLASS_BODY ... */ }

    v->phase_inc = 1.0f / (duration_ms * 44.1f);

    // Se a voz estava ativa, engatilha o fade out per-sample antes de usar os novos dados
    if(v->state == VOICE_ACTIVE) {
        v->state = VOICE_PREEMPT_FADING;
    } else {
        v->phase = 0.0f;
        v->fade_mult = 1.0f;
        v->state = VOICE_ACTIVE;
    }
}
```

## 12. What to Keep Simple in v1
Para garantir o budget do ESP32-S3 e não se perder na complexidade:
1. **Sem pitch shifting para Body bubbles:** Fixe `playback_rate = 1.0f`. O jitter só é permitido nas bolhas Micro (ataque), onde distorção harmônica não importa.
2. **Buffer de Escrita Contínuo:** Jamais limpe o áudio do delay line principal. Apenas grave o input ininterruptamente e leia de forma reativa. O "silêncio" granular é obtido não escrevendo zero no buffer, mas sim parando de spawnar vozes.
3. **Pan Centrado/Estático para Body:** Deixe as vozes de Body com pan = 0.5f (centro) na v1. Evite cálculos de seno em tempo real para automação de pan no core loop. O LFO lento de pan pode ser adicionado numa v1.1.
4. **Acumulador de Densidade Fixo:** Confie estritamente na equação `spawn_accumulator += density_delta`. Não use filas complexas de eventos pendentes. Se passou de 1.0, dê spawn; se não, aguarde.