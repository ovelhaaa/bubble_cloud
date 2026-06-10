# WASM Integration

## Papel do WASM

A build WASM expõe o core C para a UI web e para testes de paridade. O wrapper deve permanecer uma casca fina sobre `bubble_engine`, sem duplicar regras de DSP em JavaScript.

```text
Browser UI / AudioWorklet
        |
        v
WASM exports -> bubble_engine API -> macro map -> DSP config -> process
        |
        v
metrics copied to JS outside hard real-time work where possible
```

## Inicialização típica

1. Carregar módulo WASM.
2. Alocar memória linear para `BubbleEngine_t`, delay buffer e buffers de áudio.
3. Chamar função de init que usa `bubble_engine_default_config` e `bubble_engine_init`.
4. Escolher perfil (`WEB_STANDARD` por padrão em UI comum; `WEB_ULTRA` para render offline/alto desempenho).
5. Processar blocos com ponteiros para buffers mono/stereo.

## AudioWorklet e tempo real

O thread de áudio do navegador tem restrições similares a embarcado:

- Não alocar por bloco; reutilize `Float32Array`/views e ponteiros.
- Não fazer fetch, IndexedDB, localStorage, console logging intensivo ou DOM no processamento.
- Não esperar mensagens síncronas do main thread.
- Não usar locks ou `Atomics.wait` no caminho de áudio.
- Copiar parâmetros do main thread por ring buffer/snapshot e aplicar entre blocos.

## Ponte JS <-> C

- Prefira IDs públicos de parâmetro e macros normalizados.
- Valide presets em JS antes de chamar o core.
- Mantenha conversão de sample format fora do core.
- Métricas (`envelope`, estado, vozes ativas, peaks, limiter) podem ser lidas em cadência de UI, não a cada amostra.

## Determinismo e paridade

Para testes offline/WASM:

1. Use o mesmo arquivo de entrada e sample rate.
2. Inicialize com o mesmo preset e `rng_seed`.
3. Use o mesmo perfil de qualidade.
4. Evite automação dependente de tempo de wall-clock.
5. Compare áudio com tolerância numérica; pequenas diferenças de float entre toolchains são aceitáveis.

## Diferenças por perfil de qualidade no navegador

- `WEB_STANDARD` deve ser a escolha segura para playback interativo.
- `WEB_ULTRA` pode aumentar densidade/vozes e custo; use quando o dispositivo aguentar ou para render offline.
- Cair para `MCU_PLUS`/`MCU_SAFE` é permitido em dispositivos lentos, mas a UI deve indicar possível redução de densidade.
- O formato de preset, seed e significado dos macros permanecem iguais em todos os perfis.
