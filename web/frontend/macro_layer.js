(function initMacroLayer(global) {
  const MATRIX_CANDIDATE_PATHS = Object.freeze([
    '../../docs/macro_matrix.yaml',
    '/docs/macro_matrix.yaml',
    'docs/macro_matrix.yaml',
    'macro_matrix.yaml',
  ]);

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function parseMacroMatrix(text) {
    const source = String(text || '').trim();
    if (!source) throw new Error('macro_matrix.yaml está vazio.');
    return JSON.parse(source);
  }

  function fetchMatrixTextSync() {
    for (let i = 0; i < MATRIX_CANDIDATE_PATHS.length; i += 1) {
      const path = MATRIX_CANDIDATE_PATHS[i];
      try {
        const request = new XMLHttpRequest();
        request.open('GET', path, false);
        request.send(null);
        if (request.status >= 200 && request.status < 300 && request.responseText) {
          return request.responseText;
        }
      } catch (_error) {
        // tenta próximo caminho
      }
    }
    throw new Error('Não foi possível carregar docs/macro_matrix.yaml.');
  }

  function buildMatrixConfig(rawMatrix) {
    const matrix = rawMatrix && typeof rawMatrix === 'object' ? rawMatrix : {};
    const neutralMacroValue = Number(matrix.neutral_macro_value);
    const safeNeutralValue = Number.isFinite(neutralMacroValue) ? clamp(neutralMacroValue, 0, 1) : 0.5;

    const pipelineOrder = Array.isArray(matrix.application?.global_order)
      ? matrix.application.global_order.filter((entry) => typeof entry === 'string')
      : [];

    const performanceRanges = Object.freeze({ ...(matrix.performance_ranges || {}) });
    const rawMacros = matrix.macros && typeof matrix.macros === 'object' ? matrix.macros : {};

    const macroDestinations = Object.keys(rawMacros).reduce((acc, macroKey) => {
      const macroDefinition = rawMacros[macroKey] || {};
      const macroGainCompensation = Number(macroDefinition.gain_compensation);
      const gainCompensation = Number.isFinite(macroGainCompensation) ? macroGainCompensation : 1;
      const destinations = Array.isArray(macroDefinition.destinations) ? macroDefinition.destinations : [];

      acc[macroKey] = destinations
        .filter((entry) => entry && typeof entry === 'object' && typeof entry.param === 'string')
        .map((entry) => {
          const direction = String(entry.direction || 'positive').toLowerCase();
          const directionSign = direction === 'negative' ? -1 : 1;
          const baseWeight = Math.abs(Number(entry.weight) || 0);
          const safeRange = entry.safe_range || {};
          return {
            param: entry.param,
            weight: directionSign * baseWeight * gainCompensation,
            curve: entry.curve || 'linear',
            minPerf: Number.isFinite(Number(safeRange.min)) ? Number(safeRange.min) : 0,
            maxPerf: Number.isFinite(Number(safeRange.max)) ? Number(safeRange.max) : 1,
            direction,
            gainCompensation,
          };
        });
      return acc;
    }, {});

    return {
      matrix,
      neutralMacroValue: safeNeutralValue,
      pipelineOrder: Object.freeze(pipelineOrder),
      performanceRanges,
      macroDestinations: Object.freeze(macroDestinations),
      conflictRule: String(matrix.application?.conflict_rule || ''),
      canonicalPresets: Object.freeze(Array.isArray(matrix.canonical_presets) ? matrix.canonical_presets : []),
    };
  }

  function getCurveDelta(normalizedValue, curve, neutralMacroValue) {
    const parsedValue = Number(normalizedValue);
    const safeValue = Number.isFinite(parsedValue) ? parsedValue : neutralMacroValue;
    const centered = clamp(safeValue - neutralMacroValue, -0.5, 0.5) * 2;
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

  function resolvePerformanceRange(paramKey, paramDefinition, performanceRanges) {
    const def = paramDefinition || {};
    const rawMin = Number(def.min);
    const rawMax = Number(def.max);
    const profile = performanceRanges[paramKey];
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

  let matrixConfig;
  try {
    matrixConfig = buildMatrixConfig(parseMacroMatrix(fetchMatrixTextSync()));
  } catch (error) {
    console.warn('[MacroLayer] fallback para configuração vazia:', error);
    matrixConfig = buildMatrixConfig({
      neutral_macro_value: 0.5,
      application: { global_order: ['response', 'impact', 'bloom', 'smoothness', 'memory', 'width', 'scatter', 'mix'] },
      macros: {},
      performance_ranges: {},
      canonical_presets: [],
    });
  }

  function createNeutralMacroValues() {
    return matrixConfig.pipelineOrder.reduce((acc, macroKey) => {
      acc[macroKey] = matrixConfig.neutralMacroValue;
      return acc;
    }, {});
  }

  function normalizeMacroValues(rawMacroValues) {
    const normalized = createNeutralMacroValues();
    const input = rawMacroValues && typeof rawMacroValues === 'object' ? rawMacroValues : {};
    const fallbackImpactBloom = Number.isFinite(Number(input.impact_bloom)) ? Number(input.impact_bloom) : null;
    const fallbackSmoothMemory = Number.isFinite(Number(input.smoothness_memory)) ? Number(input.smoothness_memory) : null;
    const fallbackWidthScatter = Number.isFinite(Number(input.width_scatter)) ? Number(input.width_scatter) : null;

    if (fallbackImpactBloom !== null) {
      normalized.impact = fallbackImpactBloom;
      normalized.bloom = fallbackImpactBloom;
    }
    if (fallbackSmoothMemory !== null) {
      normalized.smoothness = fallbackSmoothMemory;
      normalized.memory = fallbackSmoothMemory;
    }
    if (fallbackWidthScatter !== null) {
      normalized.width = fallbackWidthScatter;
      normalized.scatter = fallbackWidthScatter;
    }

    Object.keys(normalized).forEach((macroKey) => {
      if (macroKey in input && Number.isFinite(Number(input[macroKey]))) {
        normalized[macroKey] = Number(input[macroKey]);
      }
      normalized[macroKey] = clamp(normalized[macroKey], 0, 1);
    });
    return normalized;
  }

  function applyMacroPipeline(baseParams, macroValues, paramDefinitions, perfHint = 1) {
    const resolved = { ...(baseParams || {}) };
    matrixConfig.pipelineOrder.forEach((macroKey) => {
      const macroValue = Number(macroValues?.[macroKey]);
      const targets = matrixConfig.macroDestinations[macroKey] || [];
      targets.forEach((target) => {
        const cfg = paramDefinitions[target.param];
        if (!cfg || !(target.param in resolved)) return;
        const curveDelta = getCurveDelta(macroValue, target.curve, matrixConfig.neutralMacroValue);
        const perfScale = getPerfScale(perfHint, target.minPerf, target.maxPerf);
        const perfRange = resolvePerformanceRange(target.param, cfg, matrixConfig.performanceRanges);
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
    NEUTRAL_MACRO_VALUE: matrixConfig.neutralMacroValue,
    PIPELINE_ORDER: matrixConfig.pipelineOrder,
    MACRO_DESTINATIONS: matrixConfig.macroDestinations,
    PERFORMANCE_RANGES: matrixConfig.performanceRanges,
    CONFLICT_RULE: matrixConfig.conflictRule,
    CANONICAL_PRESETS: matrixConfig.canonicalPresets,
    MATRIX_VERSION: matrixConfig.matrix?.version || 'unknown',
    MATRIX_RAW: matrixConfig.matrix,
    parseMacroMatrix,
    resolvePerformanceRange: (paramKey, paramDefinition) => resolvePerformanceRange(paramKey, paramDefinition, matrixConfig.performanceRanges),
    createNeutralMacroValues,
    normalizeMacroValues,
    applyMacroPipeline,
  };
})(window);
