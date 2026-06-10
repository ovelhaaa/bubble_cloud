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

Um preset versionado deve conter, no mínimo:

```json
{
  "schema_version": 3,
  "engine_version": "post-diffusion-ui",
  "name": "Example",
  "author": "Bubble Cloud",
  "description": "Short musical intent",
  "recommended_min_profile": "MCU_SAFE",
  "macros": {
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
  },
  "parameters": {},
  "quality": {
    "profile": "MCU_SAFE"
  },
  "rng_seed": 1
}
```

## Macros vs parâmetros brutos

- **Macros** são o contrato público principal e devem estar em espaço normalizado `0.0..1.0`.
- **Parâmetros brutos** são permitidos para presets de desenvolvimento ou migração, mas devem usar nomes canônicos do schema e valores clampados.
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

Um preset pode declarar `recommended_min_profile`. Se o host usar um perfil menor:

- Ele pode limitar vozes e reduzir densidade em passagens exigentes.
- Ele não deve mudar nomes, ranges ou significado das macros.
- Ele deve manter o mesmo seed e a mesma ordem de eventos até onde o limite de vozes permitir.
- Ele deve sinalizar ao usuário quando o perfil atual estiver abaixo do recomendado.
