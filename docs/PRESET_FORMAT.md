# Preset Format

## Objetivo

Presets descrevem uma intenção musical portátil entre UI web, render offline, WASM e targets embarcados. O formato externo recomendado é JSON; o core C também expõe `BubbleEnginePreset_t` para snapshots binários internos/controlados.

## Fluxo de preset

```text
preset JSON / platform storage
        |
        v
UI/Platform validation + migration
        |
        v
bubble_engine_load_preset / set_parameter
        |
        v
macro map
        |
        v
DSP config
        |
        v
process
```

## Campos recomendados em JSON

Um preset versionado deve usar os nomes canônicos que o parser C (`bubble_preset_load_json`) e o schema JS (`presetSchema.js`) leem e serializam: `params` para parâmetros DSP brutos e `macro_values` para macros musicais.

```json
{
  "schema_version": 3,
  "engine_version": "post-diffusion-ui",
  "preset_name": "Example",
  "description": "Short musical intent",
  "params": {
    "rng_seed": 1,
    "quality_profile": 0
  },
  "macro_values": {
    "density": 0.5,
    "bloom": 0.5,
    "motion": 0.5,
    "texture": 0.5,
    "space": 0.5,
    "gravity": 0.5,
    "memory": 0.5,
    "clarity": 0.5,
    "freeze": 0.0,
    "sparkle": 0.0,
    "warmth": 0.5,
    "mix": 0.5
  }
}
```

Campos opcionais de UI, como `preset_slug`, `created_at`, `ui_category`, `tags`, `esp32_safe`, `quality_tier` e `metadata`, podem existir nos presets de fábrica e na UI. O parser C não usa esses campos para configurar áudio; eles devem ser tratados como metadados de host.

## `macro_values` vs `params`

- **`macro_values`** é o contrato público principal para controles musicais e deve conter macros normalizados em `0.0..1.0`.
- **`params`** contém parâmetros DSP brutos usando nomes canônicos do schema, por exemplo `rng_seed`, `quality_profile` e `active_voice_limit`.
- **`rng_seed` deve ficar dentro de `params`**, porque é carregado como parâmetro interno do motor.
- **`quality_profile` deve ficar dentro de `params`** como enum numérico (`0` = `MCU_SAFE`, `1` = `MCU_PLUS`, `2` = `WEB_STANDARD`, `3` = `WEB_ULTRA`). Um objeto raiz `quality` não é processado pelo parser C.
- **Developer mode** pode revelar campos brutos; presets de usuário devem continuar legíveis em termos musicais.

## Versionamento e migração

- `schema_version` identifica a estrutura do arquivo.
- `engine_version` identifica a família de motor esperada.
- Leitores devem rejeitar versões futuras desconhecidas ou migrá-las explicitamente.
- Migrações devem preservar intenção musical e documentar mudanças de range/curva.
- Presets de fábrica devem permanecer canônicos e testados.

## Determinismo por seed

`rng_seed` faz parte do som. Para render reproduzível:

1. Defina seed antes de processar.
2. Use o mesmo perfil de qualidade.
3. Aplique parâmetros na mesma ordem relativa aos blocos de áudio.
4. Use o mesmo input e sample rate esperado pelo motor.

## Diferenças por perfil de qualidade

A qualidade efetiva para o core deve ser serializada em `params.quality_profile` e, quando necessário, `params.active_voice_limit`. Presets de fábrica também podem declarar `metadata.recommended_min_profile` para UI/validação, mas esse metadado não substitui `params.quality_profile` no parser C.

Se o host usar um perfil menor que o recomendado por metadados ou UI:

- Ele pode limitar vozes e reduzir densidade em passagens exigentes.
- Ele não deve mudar nomes, ranges ou significado das macros.
- Ele deve manter o mesmo seed e a mesma ordem de eventos até onde o limite de vozes permitir.
- Ele deve sinalizar ao usuário quando o perfil atual estiver abaixo do recomendado.
