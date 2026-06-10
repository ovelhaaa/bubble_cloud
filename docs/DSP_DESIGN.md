# DSP Design

## Modelo sonoro

O DSP cria uma nuvem de pequenos grãos/bolhas a partir de um buffer de delay mono. Cada bolha lê uma região temporal atrás do write head, aplica envelope, pan, ganho, variação de tom e regras de vida, e então soma em um bus estéreo molhado. O motor mistura seco/molhado e aplica proteção final de limiter.

## Fluxo do DSP

```text
UI/Platform -> bubble_engine API -> macro map -> DSP config -> process
```

Dentro de `process`, o fluxo conceitual é:

```text
input mono
   |
   v
write delay buffer -----> spawn scheduler -----> active bubble voices
   |                           |                         |
   |                           v                         v
   |                    read-region choice         envelope/pitch/pan
   |                                                     |
   +----------------------------- dry -------------------+
                                                         v
                                           wet bus + limiter + stereo out
```

## Estados e classes de bolha

O detector de envelope/transiente seleciona estados como silêncio, burst transiente, ataque, sustain/body e decay esparso. Esses estados alimentam classes de bolha:

- `MICRO_ATTACK`: grãos curtos e próximos ao ataque.
- `SHORT_INTERMEDIATE`: grãos intermediários para transição.
- `SUSTAIN_BODY`: grãos mais longos e distantes para corpo/memória.

A densidade efetiva é controlada por macros, estado de entrada, modo rítmico e limite ativo de vozes.

## Regiões de leitura

As regiões são distâncias atrás do write head:

- **Attack**: região curta para preservar articulação.
- **Body**: região média para textura sustentada.
- **Memory**: região longa para cauda, freeze e caráter ambiental.

O host não deve assumir que offsets brutos são estáveis como UI pública. Use macros e presets versionados quando possível.

## Determinismo

O motor usa PRNG determinístico para decisões que afetam som: classe, duração, região, pan, pitch/reverse/droplet e variações relacionadas. O contrato é:

- Mesma `rng_seed` inicial, input e sequência de parâmetros => mesmas decisões pseudoaleatórias.
- Seed `0` pode ser normalizada internamente para uma seed fixa não-zero.
- Diferenças de ponto flutuante entre compiladores/targets podem causar pequenas diferenças numéricas; testes de paridade devem comparar tolerâncias adequadas por backend.

## Contrato de tempo real

O hot path do DSP deve permanecer compatível com callback de áudio:

- Sem alocação dinâmica.
- Sem I/O.
- Sem locks ou espera por outros threads.
- Sem chamadas de sistema imprevisíveis.
- Sem dependência de UI/web/codec/storage.
- Memória de trabalho em estruturas fixas ou fornecida pelo caller.

## Diferenças permitidas por perfil de qualidade

Perfis de qualidade podem alterar apenas características de orçamento e escala controlada:

| Perfil | Diferenças permitidas |
| --- | --- |
| `MCU_SAFE` | Menor limite de vozes, menor CPU/RAM, nuvens menos densas em material extremo. |
| `MCU_PLUS` | Mais vozes que `MCU_SAFE`, mantendo orçamento embarcado conservador. |
| `WEB_STANDARD` | Densidade maior e mais headroom para browsers comuns. |
| `WEB_ULTRA` | Usa o teto compilado de vozes para renderização/web com maior CPU/RAM. |

Diferenças aceitáveis incluem número simultâneo de vozes, CPU, RAM, eventos de voice stealing e densidade percebida quando o preset excede o limite do perfil. Diferenças não aceitáveis incluem troca de significado de macro, alteração de seed, mudança de formato de preset sem migração ou introdução de I/O/alocação no áudio.
