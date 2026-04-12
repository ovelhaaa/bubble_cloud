(function initMacroLayer(global) {
  const NEUTRAL_MACRO_VALUE = 0.5;

  const PIPELINE_ORDER = ['response', 'impact_bloom', 'smoothness_memory', 'width_scatter', 'mix'];

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
        const range = Number(cfg.max) - Number(cfg.min);
        const curveDelta = getCurveDelta(macroValue, target.curve);
        const perfScale = getPerfScale(perfHint, target.minPerf, target.maxPerf);
        const offset = range * Number(target.weight || 0) * curveDelta * perfScale;
        resolved[target.param] = clamp(Number(resolved[target.param]) + offset, Number(cfg.min), Number(cfg.max));
      });
    });
    return resolved;
  }

  global.BubbleCloudMacroLayer = {
    NEUTRAL_MACRO_VALUE,
    PIPELINE_ORDER,
    MACRO_DESTINATIONS,
    createNeutralMacroValues,
    applyMacroPipeline,
  };
})(window);
