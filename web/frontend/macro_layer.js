(function initMacroLayer(global) {
  const NEUTRAL_MACRO_VALUE = 0.5;

  const PIPELINE_ORDER = ['response', 'impact_bloom', 'smoothness_memory', 'width_scatter', 'mix'];

  const PERFORMANCE_RANGES = Object.freeze({
    tracking_thresh: { min: 0.01, max: 0.12, curve: 'sCurve', softKnee: 0.12, rationale: 'Resposta rápida sem entrar em auto-trigger de ruído.' },
    transient_delta: { min: 0.02, max: 0.22, curve: 'sCurve', softKnee: 0.14, rationale: 'Sensibilidade de ataque musical antes da região hiper-seletiva.' },
    duck_attack_coef: { min: 0.55, max: 0.97, curve: 'exp', softKnee: 0.1, rationale: 'Engate do duck rápido o suficiente sem colapsar dinâmica.' },
    burst_immediate_count: { min: 2, max: 9, curve: 'linear', softKnee: 0.08, rationale: 'Ataque cheio sem densidade excessiva.' },
    density_burst: { min: 20, max: 140, curve: 'log', softKnee: 0.12, rationale: 'Faixa de energia principal do burst em contexto musical.' },
    density_sustain: { min: 8, max: 55, curve: 'log', softKnee: 0.12, rationale: 'Corpo contínuo sem wash constante.' },
    attack_brightness: { min: 0.8, max: 1.65, curve: 'sCurve', softKnee: 0.12, rationale: 'Brilho de ataque útil sem aspereza extrema.' },
    wet_drive: { min: 0.75, max: 1.45, curve: 'exp', softKnee: 0.1, rationale: 'Saturação leve a moderada com headroom.' },
    sustain_darkness: { min: 0.1, max: 0.72, curve: 'sCurve', softKnee: 0.12, rationale: 'Escurecimento progressivo sem perda de definição.' },
    memory_mix: { min: 0.12, max: 0.72, curve: 'sCurve', softKnee: 0.12, rationale: 'Memória perceptível sem engolir o presente.' },
    memory_pull: { min: 0.08, max: 0.62, curve: 'sCurve', softKnee: 0.12, rationale: 'Tração para regiões antigas com preservação de ataque atual.' },
    attack_rate_jitter_depth: { min: 0.002, max: 0.09, curve: 'exp', softKnee: 0.16, rationale: 'Humanização leve sem instabilidade de timing.' },
    stereo_width: { min: 0.3, max: 1.0, curve: 'sCurve', softKnee: 0.1, rationale: 'Abertura estéreo ampla mantendo centro estável.' },
    attack_pan_spread: { min: 0.25, max: 1.0, curve: 'sCurve', softKnee: 0.1, rationale: 'Espalhamento do ataque com foco central preservado.' },
    sustain_pan_spread: { min: 0.1, max: 0.85, curve: 'sCurve', softKnee: 0.1, rationale: 'Largura do sustain sem desfocar imagem.' },
    smart_start_range: { min: 4, max: 24, curve: 'log', softKnee: 0.12, rationale: 'Busca de zero-cross útil sem custo/latência excessivos.' },
    mix_dry_gain: { min: 0.25, max: 0.85, curve: 'sCurve', softKnee: 0.12, rationale: 'Dry equilibrado com espaço para wet reagir.' },
    mix_wet_gain: { min: 0.15, max: 0.9, curve: 'sCurve', softKnee: 0.12, rationale: 'Wet expressivo sem cobrir completamente o dry.' },
    wet_output_trim: { min: 0.55, max: 1.2, curve: 'exp', softKnee: 0.1, rationale: 'Compensação de nível pós-drive com margem de segurança.' },
    sustain_diffusion_amount: { min: 0.0, max: 0.72, curve: 'sCurve', softKnee: 0.12, rationale: 'Difusão para cauda musical sem virar pad contínuo.' },
  });

  const MACRO_DESTINATIONS = Object.freeze({
    response: [
      { param: 'tracking_thresh', weight: -0.22, curve: 'sCurve', minPerf: 0.15, maxPerf: 1.0 },
      { param: 'transient_delta', weight: -0.18, curve: 'sCurve', minPerf: 0.15, maxPerf: 1.0 },
      { param: 'duck_attack_coef', weight: -0.2, curve: 'linear', minPerf: 0.3, maxPerf: 1.0 },
      { param: 'burst_immediate_count', weight: 0.25, curve: 'sCurve', minPerf: 0.45, maxPerf: 1.0 },
    ],
    impact_bloom: [
      { param: 'density_burst', weight: 0.35, curve: 'sCurve', minPerf: 0.35, maxPerf: 1.0 },
      { param: 'density_sustain', weight: 0.2, curve: 'linear', minPerf: 0.35, maxPerf: 1.0 },
      { param: 'attack_brightness', weight: 0.16, curve: 'linear', minPerf: 0.25, maxPerf: 1.0 },
      { param: 'wet_drive', weight: 0.18, curve: 'softExp', minPerf: 0.55, maxPerf: 1.0 },
    ],
    smoothness_memory: [
      { param: 'sustain_darkness', weight: 0.22, curve: 'sCurve', minPerf: 0.2, maxPerf: 1.0 },
      { param: 'memory_mix', weight: 0.25, curve: 'sCurve', minPerf: 0.2, maxPerf: 1.0 },
      { param: 'memory_pull', weight: 0.16, curve: 'linear', minPerf: 0.2, maxPerf: 1.0 },
      { param: 'attack_rate_jitter_depth', weight: -0.2, curve: 'softExp', minPerf: 0.5, maxPerf: 1.0 },
    ],
    width_scatter: [
      { param: 'stereo_width', weight: 0.32, curve: 'sCurve', minPerf: 0.2, maxPerf: 1.0 },
      { param: 'attack_pan_spread', weight: 0.24, curve: 'sCurve', minPerf: 0.2, maxPerf: 1.0 },
      { param: 'sustain_pan_spread', weight: 0.24, curve: 'sCurve', minPerf: 0.2, maxPerf: 1.0 },
      { param: 'smart_start_range', weight: 0.2, curve: 'softExp', minPerf: 0.6, maxPerf: 1.0 },
    ],
    mix: [
      { param: 'mix_dry_gain', weight: -0.28, curve: 'linear', minPerf: 0.1, maxPerf: 1.0 },
      { param: 'mix_wet_gain', weight: 0.28, curve: 'linear', minPerf: 0.1, maxPerf: 1.0 },
      { param: 'wet_output_trim', weight: -0.14, curve: 'softExp', minPerf: 0.35, maxPerf: 1.0 },
      { param: 'sustain_diffusion_amount', weight: 0.16, curve: 'sCurve', minPerf: 0.7, maxPerf: 1.0 },
    ],
  });

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function getCurveDelta(normalizedValue, curve) {
    const parsedValue = Number(normalizedValue);
    const safeValue = Number.isFinite(parsedValue) ? parsedValue : NEUTRAL_MACRO_VALUE;
    const centered = clamp(safeValue - NEUTRAL_MACRO_VALUE, -0.5, 0.5) * 2;
    if (curve === 'softExp') {
      return Math.sign(centered) * (Math.exp(Math.abs(centered)) - 1) / (Math.E - 1);
    }
    if (curve === 'sCurve') {
      return centered * (1.25 - 0.25 * Math.abs(centered));
    }
    return centered;
  }

  function applyCurve(value, curve) {
    const v = clamp(Number(value) || 0, 0, 1);
    if (curve === 'log') return Math.log1p(v * 9) / Math.log(10);
    if (curve === 'exp') return (Math.exp(v) - 1) / (Math.E - 1);
    if (curve === 'sCurve') return v * v * (3 - 2 * v);
    return v;
  }

  function invertCurve(value, curve) {
    const v = clamp(Number(value) || 0, 0, 1);
    if (curve === 'log') return (Math.pow(10, v) - 1) / 9;
    if (curve === 'exp') return Math.log(1 + v * (Math.E - 1));
    if (curve === 'sCurve') {
      if (v <= 0) return 0;
      if (v >= 1) return 1;
      return 0.5 - Math.sin(Math.asin(1 - 2 * v) / 3);
    }
    return v;
  }

  function softClamp01(value, softKnee = 0.12) {
    const v = Number(value) || 0;
    const k = clamp(Number(softKnee) || 0.12, 0.02, 0.35);
    if (v < 0) return (k * v) / (k - v);
    if (v > 1) return 1 + (k * (v - 1)) / (k + (v - 1));
    return v;
  }

  function resolvePerformanceRange(paramKey, paramDefinition) {
    const def = paramDefinition || {};
    const rawMin = Number(def.min);
    const rawMax = Number(def.max);
    const profile = PERFORMANCE_RANGES[paramKey];
    if (!profile || !Number.isFinite(rawMin) || !Number.isFinite(rawMax) || rawMax <= rawMin) {
      return { min: rawMin, max: rawMax, curve: 'linear', softKnee: 0.12, isRawFallback: true };
    }
    return {
      min: clamp(Number(profile.min), rawMin, rawMax),
      max: clamp(Number(profile.max), rawMin, rawMax),
      curve: profile.curve || 'linear',
      softKnee: Number.isFinite(profile.softKnee) ? profile.softKnee : 0.12,
      rationale: profile.rationale || '',
      isRawFallback: false,
    };
  }

  function rawToPerformanceNorm(rawValue, perfRange) {
    const min = Number(perfRange.min);
    const max = Number(perfRange.max);
    if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) return 0.5;
    const linear = clamp((Number(rawValue) - min) / (max - min), 0, 1);
    return invertCurve(linear, perfRange.curve);
  }

  function performanceNormToRaw(normValue, perfRange) {
    const min = Number(perfRange.min);
    const max = Number(perfRange.max);
    if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) return Number.isFinite(min) ? min : 0;
    const curved = applyCurve(clamp(normValue, 0, 1), perfRange.curve);
    return min + (max - min) * curved;
  }

  function getPerfScale(perfHint, minPerf, maxPerf) {
    const safePerf = clamp(Number(perfHint), 0, 1);
    const min = Number.isFinite(minPerf) ? minPerf : 0;
    const max = Number.isFinite(maxPerf) ? maxPerf : 1;
    if (safePerf <= min) return 0;
    if (safePerf >= max) return 1;
    return (safePerf - min) / Math.max(0.0001, max - min);
  }

  function createNeutralMacroValues() {
    return PIPELINE_ORDER.reduce((acc, macroKey) => {
      acc[macroKey] = NEUTRAL_MACRO_VALUE;
      return acc;
    }, {});
  }

  function applyMacroPipeline(baseParams, macroValues, paramDefinitions, perfHint = 1) {
    const resolved = { ...(baseParams || {}) };
    PIPELINE_ORDER.forEach((macroKey) => {
      const macroValue = Number(macroValues?.[macroKey]);
      const targets = MACRO_DESTINATIONS[macroKey] || [];
      targets.forEach((target) => {
        const cfg = paramDefinitions[target.param];
        if (!cfg || !(target.param in resolved)) return;
        const curveDelta = getCurveDelta(macroValue, target.curve);
        const perfScale = getPerfScale(perfHint, target.minPerf, target.maxPerf);
        const perfRange = resolvePerformanceRange(target.param, cfg);
        const currentNorm = rawToPerformanceNorm(resolved[target.param], perfRange);
        const proposedNorm = currentNorm + Number(target.weight || 0) * curveDelta * perfScale;
        const softNorm = softClamp01(proposedNorm, perfRange.softKnee);
        const mappedRaw = performanceNormToRaw(softNorm, perfRange);

        const min = Number(cfg.min);
        const max = Number(cfg.max);
        const step = Number(cfg.step);
        let finalValue = mappedRaw;
        if (step && step > 0) {
          finalValue = Math.round((mappedRaw - min) / step) * step + min;
        }
        resolved[target.param] = clamp(finalValue, min, max);
      });
    });
    return resolved;
  }

  global.BubbleCloudMacroLayer = {
    NEUTRAL_MACRO_VALUE,
    PIPELINE_ORDER,
    MACRO_DESTINATIONS,
    PERFORMANCE_RANGES,
    resolvePerformanceRange,
    createNeutralMacroValues,
    applyMacroPipeline,
  };
})(window);
