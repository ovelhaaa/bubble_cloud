# M2 — Musical Cloud Character

**Versão:** 1.3.0 (M2.4)
**Última atualização:** 2026-09-27

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

Agora o motor tem **dois mecanismos de aleatoriedade**:

- `rng_state`: stream **por canal**, sequencial, decorrelacionado pela máscara
  `channel_decorrelation` (aplicada em `SoundBubbles_SetRngSeed`, persistindo
  entre trocas de preset/seed).
- Decisões compartilhadas **keyed por identidade canônica da proveniência do
  evento de scheduler** (M2.4): não existe stream compartilhado mutável nem
  contador global de draws/spawns. Cada caminho real do scheduler possui sua
  própria sequência de `event_index` por tick e o valor compartilhado é um hash
  stateless de
  `(shared_event_seed, scheduler_tick, source, event_index, child_index,
  decision_kind)`, calculado sob demanda e idêntico nos dois canais.

A coerência de cada decisão de spawn depende apenas da fase da frase:

| Estado | Coerência | Efeito |
| --- | --- | --- |
| `TRANSIENT_BURST` | 0.80 | ataques praticamente idênticos/alinados |
| `ATTACK_ONGOING` | 0.72 | ainda coerente |
| `SUSTAIN_BODY` | 0.22 | sustain abre |
| `SPARSE_DECAY` | 0.20 | cauda larga |
| Freeze | ≤ 0.12 | campo bem aberto |

`SharedSpawnRandom(engine, coherence, spawn_id, kind)` deriva o `roll` e o
valor candidato compartilhado da **mesma identidade canônica do evento** (o
`roll` e o valor usam lanes distintas do hash). Com probabilidade `coherence`
devolve o valor compartilhado; caso contrário devolve um draw do stream local do
canal. O helper é **puro em relação à identidade**: não incrementa contador algum
e não depende de quantas decisões anteriores ocorreram.

Porque a decisão compartilhada é endereçada pela proveniência lógica explícita e
**não pela ordem de consumo de RNG nem pela ordem de execução local de spawns**,
ela não depende de quantos spawns o outro canal executou antes — nem mesmo de
spawns top-level assimétricos dentro do mesmo tick. A identidade é
`SharedSpawnId = (tick, source, event_index, child_index)`:

```text
shared key =
(
    shared_event_seed,
    tick,
    source,
    eventIndex,
    childIndex,
    decisionKind
)
```

```text
Shared stereo identity is derived from canonical scheduler event provenance,
not from local spawn execution order.
Asymmetric top-level spawn counts therefore do not shift future shared decisions.
```

A decisão de região/tier é consumida para **todas** as classes (inclusive micro
ataques, que sempre leem a região de ataque), de modo que escolhas de classe
divergentes não afetam a identidade compartilhada de eventos seguintes. O
`kind` documenta a decisão (`class`, `region_tier`, `offset_band`).

A coerência é **estatística e controlada, não identidade total** entre engines:

- **parcialmente coerentes** (usam o valor compartilhado com probabilidade
  `coherence`): classe do bubble e decisões de categoria/região de memória
  (`memory_tier`, banda temporal do read offset);
- **canal-local** (sempre no stream sequencial do canal): `pan`, offset fino
  dentro da banda, seleção de pitch (`SHIMMER`), microdetune, duração, jitter de
  ataque e reverse.

Requisito de determinismo preservado: mesma seed + mesma entrada + mesma config
→ mesmas decisões. A sequência determinística exata difere da M2.1 porque o
sorteio compartilhado deixou de ser um stream sequencial; **distribuições e
caráter musical** são preservados, não a sequência bit-exata (a antiga era
frágil sob contagem assimétrica de spawns).

Justificativa musical: transientes mais coerentes e “punchy” quando as duas
entradas coincidem; sustain/decay/freeze abrem em um halo largo. A compatibilidade
mono é preservada e a correlação dual-mono fica controlada (ver §7).

### 2.1 Identidade canônica de evento do scheduler (M2.4)

O scheduler é o único dono da identidade. Cada caminho real de spawn top-level
tem uma **fonte canônica** e uma sequência de `event_index` própria por tick. A
identidade completa é:

```text
SharedSpawnId = (tick, source, event_index, child_index)
```

Fontes e mapeamento:

| Fonte (`source`) | Origem lógica | `event_index` | `child_index` |
| --- | --- | --- | --- |
| `DENSITY` | acumulador de densidade livre | spawn de densidade do tick | filho do burst (SPRAY/SWARM/REVERSE_SWELL/SINGLE) |
| `RHYTHM` | passo de ritmo sincronizado a tempo | passo que disparou | filho do burst |
| `STRUM` | fila de strum | evento de strum do tick | 0 |
| `BURST` | burst imediato de transiente | invocação do tick | filho do burst |
| `DROPLET` | grão de 2ª geração | identidade do parent dobrada | geração derivada |

O burst mode (SPRAY/SWARM/REVERSE_SWELL/SINGLE) é a variante da invocação e é
endereçado por `child_index`; ele não cria acoplamento entre fontes.

Semântica para spawns assimétricos, dentro de um tick:

- `density`/`rhythm`/`strum`/`burst` têm contadores independentes, então um spawn
  top-level extra de uma fonte **não desloca** as identidades das demais;
- um spawn extra sem correspondente continua válido, mas não desloca os IDs dos
  demais;
- a quantidade de spawns **não** é sincronizada artificialmente entre canais.

Exemplo robusto (independente da ordem entre sources):

```text
L: density #0, strum #12, density #1
R: density #0,             density #1
```

`density #1` recebe a mesma identidade canônica nos dois canais.

Spawn saturado: quando a pool está cheia, o request entra na `pending_spawns`
com o `SharedSpawnId` completo capturado no momento do request (incluindo o
`tick` de origem) e o reutiliza quando finalmente vira voice — **nenhum campo é
recalculado**.

Second-generation (droplets): a identidade deriva da proveniência canônica
completa do parent e de uma geração:

```text
child.source      = DROPLET
child.event_index = parent.event_index * SOURCE_COUNT + parent.source
child.child_index = DERIVED_FLAG | (parent.child_index << 8) | generation
```

O namespace próprio (`DROPLET`) e o bit derivado em `child_index` isolam os
filhos: um droplet nunca colide com o parent, com um filho de burst primário ou
com um irmão, e nunca consome nem desloca o próximo `event_index` primário do
tick.

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
que o guard seja calculado com o rate final realmente usado pelo grain. A ordem
final do spawn é:

```text
pitch mode → attack jitter → microdetune → rate final
→ Smart Start → projected_span → guard clamp → read_ptr_float
```

M2.2: Smart Start (`RefineReadOffsetSmartStart`) roda **antes** do
`ClampSpawnOffsetForGuard`, então o offset que efetivamente chega a
`read_ptr_float` é sempre o resultado do clamp (não pode ser deslocado para
dentro da banda proibida depois da proteção).

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

As mudanças ocorrem apenas no **spawn** (control-rate, ~30–80 eventos/s): o
hash stateless por decisão compartilhada (poucos xorshift/multiply, sem
alocação, lock ou estado global), um `event_index` por invocação de source e
`powf` por grain. Nenhum custo novo no laço por amostra. A M2.4 substitui o
incremento de um contador top-level global (`tick_spawn_ordinal++`) por um
`Scheduler_NextSpawnId(source)` por invocação, então o custo de CPU permanece o
mesmo. O teste de orçamento (`tests/performance`) e o smoke de bloco continuam
dentro do budget.

Regressões M2.2/M2.3/M2.4: `tests/dsp/m2_coherence_guard_harness.c` cobre (1)
divergência de `coherence` entre canais, (2) contagem desigual de spawns
reconvergindo, (3) divergência estéreo longa e assimétrica, (4) determinismo por
identidade canônica, (5) decorrelação das decisões locais, (10) a assimetria
*same-tick* com spawn derivado, (11) preservação da identidade completa na
pending queue saturada, (12) identidade derivada e sem colisão de droplets, (13)
o stress assimétrico multi-spawn de 8000 ticks, (14) a assimetria **real**
*same-tick* com spawn top-level extra (falhava na M2.3 e não depende de
derived/droplet), (15) assimetria cross-source (`density A, strum extra, density
B`), (16) assimetria de burst (SPRAY/SWARM extra entre eventos comuns), e (17) o
stress de 3000 ticks pelos **caminhos reais do scheduler** (`Scheduler_RunTick`)
com STRUM/BURST extras em L. Somam-se a regressão de Smart Start vs guard, o
guard com microdetune na matriz 44.1/48/88.2/96 kHz, o `m2_character_harness.c`,
o wrapper stereo probe, a matriz de sample rate/block e a paridade
Offline/WASM.

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
