# Bubble Cloud Roadmap

Este roadmap descreve a direção do motor compartilhado `bubble_engine`, das integrações de plataforma e dos formatos de preset. Ele é intencionalmente orientado a contratos: a API de áudio deve continuar previsível em tempo real enquanto a UI e as plataformas evoluem.

## Visão de produto

Bubble Cloud é um efeito granular/micro-looping centrado em controles musicais de alto nível. O fluxo público esperado é:

```text
UI/Platform -> bubble_engine API -> macro map -> DSP config -> process
```

- **UI/Platform**: navegador, renderizador offline, pedal ESP32 ou outro host.
- **`bubble_engine` API**: fronteira C estável para inicialização, parâmetros, presets, qualidade e processamento.
- **Macro map**: converte macros musicais normalizados para campos DSP brutos.
- **DSP config**: configuração validada, com limites por perfil de qualidade.
- **Process**: caminho de áudio por bloco, sem dependências de UI.

## Marcos planejados

### Curto prazo

1. Consolidar documentação pública de API, presets, arquitetura, WASM e porting embarcado.
2. Manter presets de fábrica sincronizados entre `core/presets/factory/` e a UI web.
3. Expandir testes de paridade offline/WASM para cobrir novos presets e perfis de qualidade.
4. Preservar orçamento de bloco do core em `BUBBLES_BLOCK_SIZE` e validar regressões de CPU.

### Médio prazo

1. Evoluir a matriz macro sem quebrar IDs públicos de parâmetro.
2. Adicionar telemetria opcional de host fora do callback de áudio.
3. Formalizar migração de presets por versão de schema.
4. Documentar targets embarcados além do ESP32 de referência quando houver portas reais.

### Longo prazo

1. Estabilizar ABI/SDK para hosts nativos.
2. Fornecer harnesses de conformidade por plataforma.
3. Permitir perfis de qualidade adicionais somente quando houver orçamento, testes e diferenças audíveis documentadas.

## Invariantes que não devem mudar sem revisão

- O callback de áudio não pode alocar, fazer I/O, bloquear em locks ou depender de serviços de UI.
- A memória de delay do motor é fornecida pelo caller/host.
- A semente (`rng_seed`) governa decisões pseudoaleatórias que afetam som.
- Perfis de qualidade podem alterar custo e densidade máxima, mas não devem mudar o significado musical de um preset.
