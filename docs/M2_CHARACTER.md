# M2 — Musical Cloud Character

**Versão:** 1.1.0 (M2.1)
**Última atualização:** 2026-09-26

Este documento descreve a milestone **M2 — Musical Cloud Character**, que
aprofunda a musicalidade, a profundidade temporal e a identidade sonora do
Bubble Cloud sem adicionar efeitos novos, sem novos controles de UI e sem
quebrar os contratos estabilizados na M1/M1.1.

Todos os recursos são internos: nenhum ID de parâmetro público foi criado ou
alterado. A única API nova é interna
(`SoundBubbles_SetChannelDecorrelation` / `bubble_engine_set_channel_decorrelation`),
usada pelo wrapper dual-engine.

## 1. Distribuição de memória temporal (recent-weighted)

Antes (`ChooseReadOffsetSamples` / `ResolveReadRegionChoice`, M1): a região era
escolhida por `memory_mix`/`memory_pull` e o offset era **uniforme** dentro da
região, com memória profunda potencialmente muito frequente.

Agora:

- Cada spawn recebe um **tier temporal** determinístico:
  - ~60% recent (body, terço recente),
  - ~25% medium (body, terço superior),
  - ~15% deep (memory_region, ghost ocasional).
- `MEMORY`/`memory_pull` e a fase da frase ajustam o peso do deep, mas existe um
  piso de material recente para não perder a conexão com a frase.
- Dentro da região escolhida, `ChooseReadOffsetSamples` divide o span em três
  bandas temporais ponderadas (recent/medium/deep). A escolha da banda é
  compartilhada entre canais via o stream de coerência; a posição dentro da banda
  é por canal (mantém diferença de read offset para largura estéreo).
- O deep memory é limitado por `BUBBLES_MEMORY_DEEP_MAX_SHARE` para permanecer
  raro mesmo com o macro `MEMORY` alto.

Justificativa musical: a nuvem continua “ligada” ao que o instrumentista acabou
de tocar, mas fantasmas ocasionais de material antigo trazem profundidade.

## 2. Coerência estéreo interna

Antes: as duas engines L/R usavam RNG totalmente decorrelacionado (o seed do
canal direito era XORado), produzindo um campo estéreo largo mas caótico.

Agora o motor tem **dois streams**:

- `rng_state`: stream por canal, decorrelacionado pela máscara
  `channel_decorrelation` (aplicada em `SoundBubbles_SetRngSeed`, persistindo
  entre trocas de preset/seed).
- `coherence_rng_state`: stream **compartilhado**, mesmo seed nos dois canais.

A coerência de cada decisão de spawn depende apenas da fase da frase:

| Estado | Coerência | Efeito |
| --- | --- | --- |
| `TRANSIENT_BURST` | 0.80 | ataques praticamente idênticos/alinados |
| `ATTACK_ONGOING` | 0.72 | ainda coerente |
| `SUSTAIN_BODY` | 0.22 | sustain abre |
| `SPARSE_DECAY` | 0.20 | cauda larga |
| Freeze | ≤ 0.12 | campo bem aberto |

`SpawnRandomFloat01(engine, coherence)` sorteia **sempre dois draws** do stream
compartilhado: o `roll` e um valor candidato (`shared_value`). Com probabilidade
`coherence` devolve o valor compartilhado; caso contrário devolve um draw do
stream local do canal. Como o número de draws compartilhados por chamada é
constante, `coherence_rng_state` de L/R **nunca perde lockstep** — mesmo que os
dois canais fiquem momentaneamente em estados de frase diferentes (e, portanto,
com `coherence` diferente), ou que um deles esteja em `ATTACK` e o outro em
`SUSTAIN`/`DECAY`. A decisão de região/tier também é consumida para **todas** as
classes (inclusive micro ataques, que sempre leem a região de ataque), de modo
que escolhas de classe divergentes não desalinhem o stream. Os streams locais
continuam decorrelacionados.

A coerência é **estatística e controlada, não identidade total** entre engines:

- **parcialmente coerentes** (usam o stream compartilhado com probabilidade
  `coherence`): classe do bubble e decisões de categoria/região de memória
  (`memory_tier`, banda temporal do read offset);
- **canal-local** (sempre no stream do canal): `pan`, offset fino dentro da banda,
  seleção de pitch (`SHIMMER`), microdetune, duração, jitter de ataque e reverse.

Requisito de determinismo preservado: mesma seed + mesma entrada + mesma config
→ mesmas decisões.

Justificativa musical: transientes mais coerentes e “punchy” quando as duas
entradas coincidem; sustain/decay/freeze abrem em um halo largo. A compatibilidade
mono é preservada e a correlação dual-mono fica controlada (ver §7).

## 3. Voicing do Sparkle

Antes: `BUBBLE_PITCH_MODE_SHIMMER` era binário (`unison` ou `+12`) e
`BUBBLE_PITCH_MODE_FIFTH` usava a quinta justa `1.5`.

Agora:

- Quinta corrigida para 12-TET exato: `2^(7/12) ≈ 1.498307`.
- Sparkle usa uma distribuição ponderada de intervalos musicais, com os extremos
  progressivamente mais raros:

  | Intervalo | Ratio | Peso a `SPARKLE=1` |
  | --- | --- | --- |
  | unison | 1.0 | 0.45 |
  | +12 (octave) | 2.0 | 0.28 |
  | +7 (fifth 12-TET) | 1.498307 | 0.18 |
  | +19 (octave+fifth) | 2.996614 | 0.09 |

- `SPARKLE=0` mantém unison puro (nenhum pitch aleatório).
- Overrides fixos (`OCTAVE_UP`, `OCTAVE_DOWN`, `FIFTH`) continuam preservados.

Justificativa musical: `SPARKLE` baixo soa natural; `SPARKLE` alto constrói uma
nuvem harmônica sem transformar cada grain em um pitch-shift óbvio.

## 4. Microdetune fixo por grain

Cada grain recebe, **no nascimento**, um microdetune fixo em cents, mantido
constante durante toda a vida (nunca um LFO/chorus):

| Classe / estado | Limite |
| --- | --- |
| `MICRO_ATTACK` | ±2 cents |
| `SHORT_INTERMEDIATE` | ±4 cents |
| `SUSTAIN_BODY` | ±6 cents |
| Freeze | ±8 cents |

O valor é escalado por `envelope_variation` (60–100%) e entra no
`quantized_rate`/`rate` **antes** do clamp de guarda de offset (M2.1), de modo
que o guard seja calculado com o rate final realmente usado pelo grain
(`pitch mode → jitter → microdetune → rate final → projected_span → guard`).

O guard de forward usa o **percurso relativo real** (`rate - 1`), não o rate
absoluto, e o guard de reverse usa `1 + |rate|`. Assim um microdetune de poucos
cents em torno de `rate = 1.0` só desloca a leitura pelos poucos samples que o
grain de fato ganha sobre o write head, sem empurrar artificialmente o read
offset para uma região distante. O clamp inclui ainda uma margem de deriva de
ponto flutuante de `read_ptr_float` para a vida prevista do grain, garantindo que
nenhum grain entre na guard zone cedo por causa do detune.

Justificativa musical: espessura/ensemble natural sem chorus perceptível.

## 5. Reverse condicionado pelo contexto

Antes: reverse era `RandomFloat01 < reverse_probability`.

Agora a probabilidade autoral vira um teto, escalado pela fase:

| Estado | Escala |
| --- | --- |
| `TRANSIENT_BURST` | 0.12 (×0.6 para micro) |
| `ATTACK_ONGOING` | 0.30 (×0.6 para micro) |
| `SUSTAIN_BODY` | 1.00 |
| `SPARSE_DECAY` | 1.70 |
| Freeze | ≥2.10 |
| memory profunda | ×1.25 |

`MOTION` continua modulando `reverse_probability` via motion LFO; o burst
`REVERSE_SWELL` continua forçando reverse.

Justificativa musical: preserva a articulação no ataque e faz a nuvem
“desenrolar” para trás depois do evento.

## 6. Contratos preservados

Não foram alterados: lei de soma estéreo `1/sqrt(2)`, localidade do dry, limiter
final compartilhado, invariância de sample rate, transporte BPM/PPQ, voice
stealing, agendamento de STRUM e a paridade offline/WASM. Não há alocação, locks
ou I/O no callback.

## 7. Métricas antes/depois

`scripts/m2_validation.py` compila `tests/juce/m2_validation_probe.cpp` contra a
árvore baseline (via `git worktree`) e a árvore atual, renderizando os cenários
(memory/sparkle/motion baixo-alto, freeze, mono/stereo) e reportando RMS, peak,
correlação estéreo, side/mid, vozes ativas e gain reduction do limiter.

Destaques (M1 → M2, trecho de frase determinístico a 48 kHz):

| Cenário (mono) | RMS | Correlação | Side/Mid |
| --- | --- | --- | --- |
| defaults | 0.0473 → 0.0457 | 0.997 → 0.989 | 0.040 → 0.079 |
| sparkle_high | 0.0465 → 0.0474 | 0.997 → 0.985 | 0.038 → 0.088 |
| freeze | 0.0460 → 0.0464 | 0.996 → 0.993 | 0.042 → 0.059 |

| Cenário (stereo) | RMS | Correlação | Side/Mid |
| --- | --- | --- | --- |
| defaults | 0.0472 → 0.0471 | 0.099 → 0.070 | 0.905 → 0.932 |
| memory_low | 0.0525 → 0.0489 | 0.272 → 0.103 | 0.756 → 0.902 |
| memory_high | 0.0456 → 0.0470 | 0.048 → 0.081 | 0.953 → 0.923 |

- Nível wet praticamente inalterado (≤ ~0.5 dB na maioria; offline real ficou
  ~+0.8 dB em material contínuo, medido com o renderer).
- Largura estéreo ligeiramente maior; sem colapso mono nem explosão de side.
- Limiter não é usado como compensação: gain reduction ≈ 1.0 nos cenários de
  frase (sem atuação). Em teste sintético de degrau DC o limiter trabalha mais
  por causa da coerência de ataque, que é intencional.

## 8. CPU

As mudanças ocorrem apenas no **spawn** (control-rate, ~30–80 eventos/s): uma
mão de draws extras de RNG (M2.1 fixa em dois draws compartilhados por chamada),
`powf` por grain e seleção de tier. Nenhum custo novo no laço por amostra. O
teste de orçamento (`tests/performance`) e o smoke de bloco continuam dentro do
budget.

Regressões M2.1: `tests/dsp/m2_coherence_guard_harness.c` (lockstep L/R sob
divergência temporária de estado + guard com microdetune), além do
`m2_character_harness.c`, wrapper stereo probe e matriz 44.1/48/88.2/96 kHz ×
32/64/127/256/512/2048.

## 9. Riscos remanescentes

- A coerência de ataque pode aumentar o pico do bus final quando as duas
  entradas são idênticas (dual-mono) ou degraus DC; o limiter compartilhado
  absorve e se recupera, mas o headroom efetivo em material extremo deve ser
  observado.
- A distribuição recent-weighted aumenta a presença de material recente; em
  presets com `MEMORY` alto a mudança de timbre é esperada (deep memory mais
  raro).
- Sem novos parâmetros de UI, ajustes finos ficam nos constantes internos e no
  macro `MEMORY`/`SPARKLE`/`MOTION`.
