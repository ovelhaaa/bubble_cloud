(function initPresetValidation(global) {
  function normalizePreset(canonicalPreset, paramDefinitions, baselineParams) {
    const warnings = [];
    const normalized = {
      ...canonicalPreset,
      params: { ...baselineParams, ...(canonicalPreset.params || {}) },
      base_params: { ...baselineParams, ...(canonicalPreset.base_params || canonicalPreset.params || {}) },
      macro_values: canonicalPreset.macro_values && typeof canonicalPreset.macro_values === 'object' ? { ...canonicalPreset.macro_values } : {},
      metadata: { ...(canonicalPreset.metadata || {}) },
    };

    Object.keys(paramDefinitions).forEach((key) => {
      const cfg = paramDefinitions[key];
      const rawValue = Number(normalized.params[key]);
      let value = Number.isFinite(rawValue) ? rawValue : baselineParams[key];
      const clamped = Math.min(cfg.max, Math.max(cfg.min, value));
      if (clamped !== value) warnings.push(`Campo ${key} fora da faixa e ajustado.`);
      normalized.params[key] = clamped;

      const rawBaseValue = Number(normalized.base_params[key]);
      let baseValue = Number.isFinite(rawBaseValue) ? rawBaseValue : baselineParams[key];
      const clampedBase = Math.min(cfg.max, Math.max(cfg.min, baseValue));
      if (clampedBase !== baseValue) warnings.push(`Campo base_params.${key} fora da faixa e ajustado.`);
      normalized.base_params[key] = clampedBase;
    });

    const pairs = [
      ['attack_region_min_offset_samples', 'attack_region_max_offset_samples'],
      ['body_region_min_offset_samples', 'body_region_max_offset_samples'],
      ['memory_region_min_offset_samples', 'memory_region_max_offset_samples'],
      ['micro_duration_ms_min', 'micro_duration_ms_max'],
      ['short_duration_ms_min', 'short_duration_ms_max'],
      ['body_duration_ms_min', 'body_duration_ms_max'],
    ];

    pairs.forEach(([minKey, maxKey]) => {
      if (normalized.params[minKey] > normalized.params[maxKey]) {
        normalized.params[maxKey] = normalized.params[minKey];
        warnings.push(`Ordem inválida ajustada: ${minKey} <= ${maxKey}.`);
      }
    });

    const allowedTiers = ['ESP32_SAFE', 'HIGH_QUALITY'];
    if (!allowedTiers.includes(normalized.quality_tier)) {
      normalized.quality_tier = 'ESP32_SAFE';
      warnings.push('quality_tier inválido ajustado para ESP32_SAFE.');
    }

    return { normalized, warnings };
  }

  function validatePreset(preset, paramDefinitions) {
    const errors = [];
    const warnings = [];
    if (!preset || typeof preset !== 'object') errors.push('Preset inválido: objeto ausente.');
    if (!preset?.params || typeof preset.params !== 'object') errors.push('Preset inválido: campo params ausente.');

    if (preset?.params && typeof preset.params === 'object') {
      Object.keys(preset.params).forEach((key) => {
        const value = preset.params[key];
        if (!paramDefinitions[key]) {
          warnings.push(`Parâmetro desconhecido preservado em metadata.extra: ${key}.`);
          return;
        }
        if (!Number.isFinite(Number(value))) errors.push(`Parâmetro ${key} não é numérico.`);
      });
    }
    return { errors, warnings };
  }

  global.BubbleCloudPresetValidation = { normalizePreset, validatePreset };
})(window);
