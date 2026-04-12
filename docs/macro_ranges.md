# Macro Performance Ranges (UI Layer)

## Objetivo

Este documento define as **performance ranges** usadas na camada de UI/macros. Elas são subfaixas musicais dentro dos limites técnicos do engine, para melhorar previsibilidade e manter os extremos com comportamento mais suave.

- O engine mantém os limites brutos originais (`min/max`) em `PARAM_DEFINITIONS`.
- A macro layer mapeia para subfaixas musicais por parâmetro.
- Um modo **Advanced: raw range** permite acessar a faixa completa apenas na camada profunda da UI.

## Tabela de performance ranges

| Parâmetro | Technical range (engine) | Performance range (UI/macro) | Curva | Soft-knee | Rationale musical |
|---|---:|---:|---|---:|---|
| `tracking_thresh` | `0.0–0.2` | `0.01–0.12` | s-curve | 0.12 | Resposta rápida sem auto-trigger de ruído. |
| `transient_delta` | `0.0–0.5` | `0.02–0.22` | s-curve | 0.14 | Captura de ataque musical sem seletividade extrema. |
| `duck_attack_coef` | `0.0–1.0` | `0.55–0.97` | exp | 0.10 | Engate de duck útil sem colapso dinâmico. |
| `burst_immediate_count` | `1–12` | `2–9` | linear | 0.08 | Ataque cheio sem saturar densidade. |
| `density_burst` | `0–200` | `20–140` | log | 0.12 | Densidade principal do burst com controle fino em baixa faixa. |
| `density_sustain` | `0–100` | `8–55` | log | 0.12 | Sustain denso sem wash constante. |
| `attack_brightness` | `0.3–2.0` | `0.8–1.65` | s-curve | 0.12 | Brilho definido sem aspereza. |
| `wet_drive` | `0.1–2.0` | `0.75–1.45` | exp | 0.10 | Saturação leve/moderada com headroom. |
| `sustain_darkness` | `0.0–1.0` | `0.1–0.72` | s-curve | 0.12 | Escurece sustain mantendo inteligibilidade. |
| `memory_mix` | `0.0–1.0` | `0.12–0.72` | s-curve | 0.12 | Memória presente sem dominar o now. |
| `memory_pull` | `0.0–1.0` | `0.08–0.62` | s-curve | 0.12 | Tração para memory sem perder ataque atual. |
| `attack_rate_jitter_depth` | `0.0–0.2` | `0.002–0.09` | exp | 0.16 | Humanização leve sem instabilidade perceptível. |
| `stereo_width` | `0.0–1.0` | `0.3–1.0` | s-curve | 0.10 | Abertura ampla preservando centro. |
| `attack_pan_spread` | `0.0–1.0` | `0.25–1.0` | s-curve | 0.10 | Ataque espaçoso com foco. |
| `sustain_pan_spread` | `0.0–1.0` | `0.1–0.85` | s-curve | 0.10 | Sustain largo sem desfocar imagem. |
| `smart_start_range` | `0–32` | `4–24` | log | 0.12 | Busca de zero-cross útil com custo controlado. |
| `mix_dry_gain` | `0.0–1.0` | `0.25–0.85` | s-curve | 0.12 | Dry equilibrado com espaço para bloom. |
| `mix_wet_gain` | `0.0–1.0` | `0.15–0.9` | s-curve | 0.12 | Wet expressivo sem mascarar dry. |
| `wet_output_trim` | `0.0–1.5` | `0.55–1.2` | exp | 0.10 | Compensação pós-drive sem clipping frequente. |
| `sustain_diffusion_amount` | `0.0–1.0` | `0.0–0.72` | s-curve | 0.12 | Difusão de cauda sem virar pad contínuo. |

## Estratégia de mapeamento

1. Converter valor atual raw para espaço normalizado de performance (`0..1`) por parâmetro.
2. Aplicar influência da macro nesse espaço (não diretamente em `min/max` bruto).
3. Aplicar **soft-clamp** nas bordas para evitar “batida seca” nos extremos.
4. Converter de volta para raw e só então aplicar quantização de `step` + clamp técnico final.

## Curvas

- **linear**: resposta direta.
- **log**: maior resolução próxima ao início da faixa (útil para densidade/range).
- **exp**: maior resolução no topo (útil para controles de drive e coeficientes sensíveis).
- **s-curve**: resposta suave no centro e compressão progressiva nas bordas.

## Garantia de compatibilidade

- Limites de engine continuam autoritativos.
- Presets existentes continuam válidos.
- Parâmetros sem `performance_range` explícita continuam operando em faixa técnica integral.
