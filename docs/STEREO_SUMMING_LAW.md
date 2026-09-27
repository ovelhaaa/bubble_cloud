# Lei de soma estéreo (wet summing law)

**Versão:** 1.0.0
**Última atualização:** 2026-09-26

Este documento define a lei explícita de soma do bus estéreo da arquitetura
dual-engine da M1 e as regras de telemetria associadas.

## 1) Barramento final

Cada engine mono-in/stereo-out processa um canal de entrada e produz o próprio
campo wet estéreo:

- `engineL` (entrada esquerda) produz `wetLL` (esquerda) e `wetLR` (direita);
- `engineR` (entrada direita) produz `wetRL` (esquerda) e `wetRR` (direita).

O wrapper soma os dois campos espaciais e mantém o dry estritamente local ao
canal de origem. A lei é:

```
L = dryL + wetSumGain * (wetLL + wetRL)
R = dryR + wetSumGain * (wetLR + wetRR)
wetSumGain = 1 / sqrt(2) = 0.70710678118654752440
```

O dry nunca é escalado nem duplicado: com `MIX=0` a saída é exatamente o sinal
de entrada no canal correspondente.

## 2) Justificativa do `1/sqrt(2)`

- Os dois engines usam estados de RNG distintos (o seed do engine direito é
  decorrelacionado no `prepare`), portanto os dois campos wet somam em
  **potência**, não em amplitude.
- Para duas fontes de mesma potência `P`, a soma em amplitude pura resultaria em
  ganho indefinido (até +6 dB se correlacionadas). Com `1/sqrt(2)` cada campo
  contribui com `P/2` e a soma decorrelacionada totaliza exatamente `P`.
- Isso conserva a **energia wet total da arquitetura de campo único** usada antes
  da correção estéreo, tanto para `L-only`/`R-only` quanto para dual-mono e
  estéreo independente:

  | Entrada         | Wet total (referência pré-M1) | Wet total com `1/sqrt(2)` |
  | --------------- | ---------------------------- | ------------------------- |
  | uma fonte (L)   | `a`                          | `a`                       |
  | dual-mono `L=R` | `2a`                         | `2a`                      |

- A razão largura (`side/mid`) do campo wet é invariante ao fator escalar, então
  a largura estéreo introduzida pela M1 é preservada.
- Presets antigos não sofrem ganho estrutural: o bus wet volta ao nível da
  referência de campo único em vez de +3 dB pela existência das duas engines.

A lei é implementada em `BubbleCloudEngineWrapper::sumStereoBus` e a constante
`BubbleCloudEngineWrapper::wetSumGain`; não há normalização dinâmica dependente
de conteúdo.

## 3) Limiter compartilhado

- Um único limiter final roda **depois** da soma, sobre o bus estéreo somado
  (`bubble_engine_apply_final_limiter` em `engineL`).
- O limiter não é usado como compensador de ganho estrutural: a normalização
  `1/sqrt(2)` mantém o nível wet previsível e o limiter só age em picos
  genuínos.
- `SoundBubbles_ApplyFinalLimiter` retorna o ganho mínimo observado no bloco,
  permitindo telemetria de gain reduction do bus final sem alocação.

## 4) Telemetria do bus final

`peakLeft`, `peakRight`, `limiterGain` e `clipCount` do `BubbleCloudTelemetry`
são medidos **após** a soma e o limiter compartilhado, isto é, representam o
sinal entregue ao host:

- `peakLeft`/`peakRight`: pico pós-limiter (hold entre snapshots);
- `limiterGain`: menor ganho do limiter final no intervalo (1.0 = sem atuação);
- `clipCount`: amostras do bus final acima do ceiling desde o último snapshot.

Os valores pré-limiter por engine (métricas internas do core) não são mais
publicados como telemetria final.

## 5) Testes

- `tests/juce/wrapper_stereo_probe.cpp`: aritmética exata da lei, localidade do
  dry, cruzamento do wet, largura por `SPACE`, limite de loudness dual-mono,
  comportamento/recuperação do limiter e matriz 44.1/48/88.2/96 kHz com blocos
  32/64/127/256/512/2048.
- `tests/juce/processor_smoke.cpp`: contrato estéreo via APVTS e transporte
  BPM/PPQ com playhead simulado.
- `tests/unit/test_juce_plugin_static_guards.py`: guardas da lei e da telemetria.

## 6) Atualização M2 — coerência interna entre canais

A partir da M2, os dois engines **não são mais totalmente decorrelacionados**.
Cada engine mantém um stream espacial próprio (`rng_state`, decorrelacionado por
`channel_decorrelation`) e compartilha **decisões keyed por identidade explícita
de spawn** (M2.3): o scheduler atribui um `spawnOrdinal` por spawn lógico e o
valor compartilhado é um hash stateless de
`(base seed, scheduler_tick, spawn_ordinal, decision kind)`. A coerência é
**estatística e controlada, não identidade total**: a classe do bubble e as
decisões de categoria/região de memória (`tier`, banda temporal do offset) usam o
valor compartilhado com probabilidade `coherence`; `pan`, offset fino, seleção de
pitch, microdetune, duração, jitter de ataque e reverse permanecem canal-local. A
fração compartilhada é função da fase da frase: ataques quase alinhados;
sustain/decay/freeze progressivamente independentes.

M2.2 substituiu o stream compartilhado sequencial (`coherence_rng_state`) por
decisões endereçáveis. M2.3 fechou a última fragilidade: em vez de um
`tick_shared_ordinal++` implícito (que ainda podia divergir *dentro* do mesmo
tick quando um canal executava um spawn extra entre dois eventos comuns), o
scheduler passa a atribuir um `spawnOrdinal` **explícito** por spawn lógico, e
droplets derivam uma identidade estável do parent
(`BUBBLES_SPAWN_DERIVED_FLAG | (parentOrdinal << 8) | generation`) sem consumir
nem deslocar os ordinais primários. O helper `SharedSpawnRandom` é puro: não
incrementa contador e não depende da ordem em que os draws aconteceram.

```text
Shared decisions are keyed by explicit logical spawn identity,
not by RNG consumption order.
Asymmetric spawn counts therefore do not shift future shared decisions.
```

Isso torna a coerência robusta a envelopes L/R diferentes, densidades diferentes,
preempção assimétrica e spawns extras no mesmo tick (situações normais em stereo
real). A sequência determinística exata muda em relação à M2.1/M2.2; distribuições
e caráter musical são preservados.

A lei de soma `1/sqrt(2)` permanece inalterada e continua conservando a energia
wet total quando os campos são decorrelacionados. Com coerência parcial, uma
pequena parcela correlacionada soma em amplitude; a lei ainda limita o dual-mono
(o wrapper probe valida `dualMono <= singleEngine * 1.5`) e o limiter
compartilhado absorve picos genuínos — nunca é usado como compensador de ganho.
Para ler offsets exatos, veja `docs/M2_CHARACTER.md`.
