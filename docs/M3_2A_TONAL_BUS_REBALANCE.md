# M3.2A — Tonal Bus Rebalance

**Versão:** 1.0.0 (M3.2A)
**Última atualização:** 2026-09-29

Esta milestone corrige o split tonal excessivamente agressivo entre o bus de
ataque e o bus de sustain. É **exclusivamente tonal**: não altera interpolação,
tail architecture, feedback, FDN, phase correlation, scheduler, spawn timing,
Freeze, memory tiers, Sparkle, microdetune, reverse, coerência estéreo,
droplets, limiter, normalização wet, difusão, presets nem UI.

## 1. Comportamento anterior (documentado antes da mudança)

| Elemento | Local | Valor |
| --- | --- | --- |
| `attack_hpf_l/r` init | `core/dsp/sound_bubbles_dsp.c` (`SoundBubbles_Init`) | cutoff **1500 Hz** (1-pole) |
| `sustain_lpf_l/r` init | `core/dsp/sound_bubbles_dsp.c` (`SoundBubbles_Init`) | cutoff **2000 Hz** (1-pole) |
| Recálculo de coeficientes | `CalculateFilterCoeffsLPF` | apenas na inicialização (e reseta `z1`) |
| Processamento HPF | `Filter1Pole_ProcessHPF` | `x - LPF(x)` |
| Processamento LPF | `Filter1Pole_ProcessLPF` | `y = b0*x + a1*z1` |
| `attack_tilt` / `sustain_tilt` | loop de mix em `ProcessBlockInternal` | `1.08/0.92` (attack), `0.95/1.06` (sustain/decay), `1.0/1.0` (silencio) |
| `attack_brightness` | `Voice_SpawnInit` (`tone_profile == 0`) | default **1.15**, clamp `0.3..1.8` |
| `sustain_darkness` | `Voice_SpawnInit` (`tone_profile == 2`) | default **0.25**, clamp `1-darkness → 0.2..1.0` |
| `WARMTH` | `core/engine/bubble_macro_map.c` | multiplica `attack_brightness` (`×1.12→0.82`) e soma `sustain_darkness` (`+0→0.18`); também move `wet_clip_amount` |
| `CLARITY` | `core/engine/bubble_macro_map.c` | `attack_brightness 0.80→1.65`, `sustain_darkness 0.72→0.10` |
| `BLOOM` | `core/engine/bubble_macro_map.c` | difusão do sustain + `wet_drive` + `wet_output_trim` (não toca nos cutoffs) |
| Estado da frase | `UpdateStateAndDensity` | `tilt`, wet presence, densidade, coerência |

O filtro de sustain não era recalculado em runtime; somente o init definia os
coeficientes. Combinado com o HPF de 1500 Hz, o resultado perceptivo era:

```text
attack → fino / clicky (perde 220–440 Hz)
sustain → escuro / abafado (perde presença acima de ~2 kHz)
```

## 2. Novo HPF do attack bus

- Cutoff único **`ATTACK_HPF_CUTOFF_HZ = 300 Hz`** (faixa de calibração aceita
  200–400 Hz). Continua 1-pole, fixo nesta milestone (requisito de simplicidade).
- Objetivo: remover rumble/DC e low-end excessivo sem transformar o attack em um
  “tic” fino. Fundamental e médios-baixos são preservados.

## 3. Novo LPF de sustain (dinâmico)

- Base/init **`SUSTAIN_LPF_BASE_HZ = 5000 Hz`** (faixa de calibração 4–6 kHz).
- Cutoff dinâmico dentro de **`SUSTAIN_LPF_MIN_HZ = 3500 Hz`** a
  **`SUSTAIN_LPF_MAX_HZ = 7000 Hz`**, 1-pole.

### Fórmula de modulação

```text
state_open =
    TRANSIENT_BURST : 1.00
    ATTACK_ONGOING  : 0.90
    SUSTAIN_BODY    : 0.50
    SPARSE_DECAY    : 0.25
    SILENCE         : 0.10

darkness = clamp01(config.sustain_darkness)   // WARMTH resolvido pelo macro map
openness = state_open * lerp(1.0, 1.0 - 0.65, darkness)
target_hz = 3500 + (7000 - 3500) * clamp01(openness)
```

Leitura: ataque/transiente mantém o LPF mais aberto; sustain fica intermediário;
sparse decay fecha progressivamente; WARMTH baixo (darkness baixo) abre e WARMTH
alto escurece. O macro map dobra o `WARMTH` em `sustain_darkness`
(`+0 → +0.18`) e o `CLARITY` também atua no mesmo campo resolvido
(`0.72 → 0.10`), então o DSP usa o valor resolvido como proxy tonal sem precisar
conhecer o índice do macro.

## 4. Suavização (control-rate)

- Um one-pole em control-rate suaviza o cutoff:
  `coef = 1 - exp(-(BUBBLES_BLOCK_SIZE / sample_rate) / 0.020)` (~20 ms).
- Os coeficientes `a1/b0` são recalculados **apenas quando o cutoff suavizado
  mudou ≥ `SUSTAIN_LPF_COEFF_EPSILON_HZ = 1 Hz`**, via `UpdateFilterCoeffsLPF`,
  que **preserva `z1`** (nunca reseta o estado do filtro → sem click/zipper).
- `expf()` roda no máximo algumas vezes por bloco de controle; nenhum `expf()`
  por voz ou por amostra. O estado é atualizado em `UpdateSustainBusTone`, chamado
  uma vez por control-tick após `UpdateStateAndDensity`.

## 5. O que foi preservado

- `attack_tilt` / `sustain_tilt`: ranges inalterados (não foram usados para
  mascarar o problema).
- `attack_brightness`, `sustain_darkness`, WARMTH/BLOOM/CLARITY: mapeamento
  preservado (o `sustain_darkness` passou a alimentar também o cutoff, mas
  continua alimentando o ganho tonal por grão como antes).
- Toda a arquitetura de scheduler, interpolação, Freeze, memória, difusão,
  droplets, limiter, coerência estéreo, presets e UI.

## 6. Medições por frequência (48 kHz)

Ganho do bus após o filtro, lido dos coeficientes configurados no engine
(`engine.attack_hpf_*` / `engine.sustain_lpf_*`):

| Hz | Attack HPF novo | Attack HPF antigo (1500) | Sustain LPF novo | Sustain LPF antigo (2000) |
| --- | --- | --- | --- | --- |
| 100 | −10.1 dB | −23.5 dB | −0.00 dB | −0.01 dB |
| 220 | −4.8 dB | −16.8 dB | −0.01 dB | −0.05 dB |
| 440 | −1.8 dB | −11.0 dB | −0.03 dB | −0.18 dB |
| 1k | −0.5 dB | −5.1 dB | −0.16 dB | −1.0 dB |
| 2k | −0.3 dB | −2.2 dB | −0.6 dB | −3.0 dB |
| 4k | −0.2 dB | −1.0 dB | −2.1 dB | −7.0 dB |
| 6k | −0.2 dB | −0.6 dB | −3.7 dB | −9.5 dB |
| 10k | −0.2 dB | −0.2 dB | −6.4 dB | −14.2 dB |

Bandas:

| Banda | Attack (novo) | Sustain (novo) |
| --- | --- | --- |
| 80–250 Hz | −7.4 dB | −0.0 dB |
| 250–800 Hz | −1.8 dB | −0.0 dB |
| 800 Hz–2 kHz | −0.5 dB | −0.2 dB |
| 2–5 kHz | −0.2 dB | −1.3 dB |
| 5–10 kHz | −0.2 dB | −5.0 dB |

O attack deixa de perder quase todo o conteúdo de 220–440 Hz; o sustain mantém
2–4 kHz e continua caindo gradualmente acima da região de presença.

## 7. Harness objetivo

`tests/dsp/m3_2a_tone_harness.c` (+ runner `tests/dsp/test_m3_2a_tone.py`) mede,
por frequência, o ganho de cada bus **antes/depois** do filtro (antes = 0 dB de
referência), a continuidade por bandas, a modulação dinâmica por estado/WARMTH,
o limite de suavização por tick (≤ 300 Hz) e a invariância de sample rate
(44.1/48/88.2/96 kHz).

## 8. A/B musical

`scripts/m3_2a_tonal_rebalance.py` renderiza material `pluck` (guitarra),
`pad` (sustain) e `transient` (curto) com o mesmo preset (`neutral`), seed e
input, comparando árvore baseline (`HEAD`) e árvore atual. Métricas: RMS, peak,
centroide espectral, energia por banda, continuidade transiente→sustain e limiter
GR. Resultados (resumo, 44.1 kHz):

| Material | Métrica | Baseline | Candidate | Delta |
| --- | --- | --- | --- | --- |
| pluck | RMS | 0.0688 | 0.0690 | +0.0002 |
| pluck | Peak | 0.4450 | 0.4339 | −0.0111 |
| pluck | Centroide | 1077.9 Hz | 1114.5 Hz | +36.5 Hz |
| pluck | Continuidade | 0.916 | 0.915 | −0.001 |
| pluck | Band 2–5k | 21.75 dB | 22.05 dB | +0.30 dB |
| pluck | Band 5–10k | 6.90 dB | 7.30 dB | +0.40 dB |
| pad | Centroid | 211.9 Hz | 214.7 Hz | +2.8 Hz |
| pad | Band 2–5k | −34.53 dB | −32.92 dB | +1.61 dB |
| pad | Band 5–10k | −37.69 dB | −36.53 dB | +1.16 dB |
| transient | Peak | 0.7277 | 0.7277 | +0.0000 |
| transient | Continuidade | 0.9955 | 0.9949 | −0.001 |
| todos | Limiter GR | 0.0 dB | 0.0 dB | +0.0 dB |

Nível global praticamente inalterado (≤ ~0.03 dB de RMS), sem atuação do limiter
e com mais presença (2–10 kHz) no sustain — as camadas passam a soar como partes
da mesma fonte.

## 9. Performance

O custo permanece praticamente idêntico: uma atualização de cutoff e no máximo um
par de `expf()` por control-tick (32 amostras), substituindo o cutoff fixo.
Nenhum filtro novo por voz, FFT, alocação, lock ou cálculo caro por amostra. O
teste de orçamento (`tests/performance`) permanece dentro do budget.

## 10. Regressões

Cobertura: M2 (`m2_character_harness`, `m2_coherence_guard_harness`), M3
(`m3_freeze_harness`), wrapper stereo probe, matriz SR/block, paridade
Offline/WASM, smoke JUCE e benchmark de bloco. Nenhuma tolerância foi relaxada.

## 11. Limitações

- O `WARMTH` é aplicado via `sustain_darkness` resolvido; um ajuste mais forte
  exigiria um novo campo de configuração (fora do escopo desta milestone).
- O attack HPF permanece fixo nesta etapa; adaptação por CLARITY/estado fica
  para uma milestone posterior.
