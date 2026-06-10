# Contributing

Obrigado por contribuir com Bubble Cloud. Este projeto prioriza áudio em tempo real, portabilidade e presets musicais reproduzíveis.

## Fluxo de desenvolvimento

```text
UI/Platform change
        |
        v
bubble_engine API contract
        |
        v
macro map update (if needed)
        |
        v
DSP config validation
        |
        v
process + tests
```

Antes de abrir mudanças:

1. Entenda se a alteração pertence ao core, plataforma, UI, preset ou teste.
2. Mantenha fronteiras: UI/plataforma não deve vazar para o DSP.
3. Atualize documentação quando mudar API, presets, macro map, qualidade ou contrato de tempo real.
4. Rode testes relevantes e registre limitações de ambiente.

## Regras de tempo real

Qualquer código alcançável por `bubble_engine_process` deve obedecer:

- Sem alocação dinâmica no áudio.
- Sem I/O no áudio.
- Sem locks no áudio.
- Sem chamadas potencialmente bloqueantes.
- Memória caller-owned para buffers grandes e delay.
- Determinismo por seed para decisões pseudoaleatórias que afetam som.

Se uma feature precisa de storage, logging, UI, rede ou locks, implemente fora do callback de áudio e entregue ao core como snapshot simples.

## Perfis de qualidade

Mudanças em perfis devem documentar diferenças permitidas:

- Limite ativo de vozes.
- CPU máxima esperada.
- RAM máxima esperada.
- Densidade percebida/voice stealing quando saturado.

Não é permitido que um perfil mude formato de preset, nomes/ranges de macro, contrato de seed ou semântica principal do efeito.

## Presets

- Prefira macros normalizados para presets de usuário.
- Use parâmetros brutos apenas quando houver justificativa e validação.
- Inclua `schema_version`, `engine_version`, `recommended_min_profile` e `rng_seed` quando aplicável.
- Garanta que presets de fábrica continuem passando validações.

## Testes recomendados

- Testes de unidade/estáticos para contratos de core.
- Testes de presets para ranges, schema e peaks.
- Testes de estabilidade numérica.
- Testes de performance de bloco.
- Paridade offline/WASM quando mexer no wrapper ou no DSP.

## Estilo de contribuição

- Faça mudanças pequenas e focadas.
- Prefira nomes canônicos existentes de parâmetros.
- Não esconda falhas de import/include com `try/catch` ou equivalentes.
- Comente decisões de DSP quando a intenção musical não for óbvia.
- Evite dependências novas no core sem necessidade forte.
