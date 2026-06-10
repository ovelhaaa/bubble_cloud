(function initPresetSchema(global) {
  // Manually synchronized with core/schema/bubble_preset_schema.h.
  const SCHEMA_VERSION = 3;
  const ENGINE_VERSION = 'post-diffusion-ui';
  const DEFAULT_CATEGORY = 'Utility';
  const DEFAULT_QUALITY_TIER = 'ESP32_SAFE';

  const PARAM_SPECS = [
    { id: 'BUBBLE_ENGINE_PARAM_NOISE_FLOOR', name: 'noise_floor', min: 0, max: 1, defaultValue: 0.001, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_TRACKING_THRESH', name: 'tracking_thresh', min: 0, max: 1, defaultValue: 0.01, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH', name: 'sustain_thresh', min: 0, max: 1, defaultValue: 0.05, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA', name: 'transient_delta', min: 0, max: 1, defaultValue: 0.05, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL', name: 'duck_burst_level', min: 0, max: 1, defaultValue: 0.2, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF', name: 'duck_attack_coef', min: 0, max: 1, defaultValue: 0.8, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF', name: 'duck_release_coef', min: 0, max: 1, defaultValue: 0.99, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS', name: 'burst_duration_ticks', min: 0, max: 1000, defaultValue: 10, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT', name: 'burst_immediate_count', min: 0, max: 128, defaultValue: 3, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_DENSITY_BURST', name: 'density_burst', min: 0, max: 500, defaultValue: 50, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN', name: 'density_sustain', min: 0, max: 500, defaultValue: 15, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DENSITY_DECAY', name: 'density_decay', min: 0, max: 500, defaultValue: 5, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES', name: 'attack_region_min_offset_samples', min: 0, max: 30000, defaultValue: 441, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES', name: 'attack_region_max_offset_samples', min: 64, max: 60000, defaultValue: 3528, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES', name: 'body_region_min_offset_samples', min: 128, max: 90000, defaultValue: 3528, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES', name: 'body_region_max_offset_samples', min: 512, max: 120000, defaultValue: 11025, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES', name: 'memory_region_min_offset_samples', min: 512, max: 150000, defaultValue: 11025, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES', name: 'memory_region_max_offset_samples', min: 1024, max: 220500, defaultValue: 39690, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN', name: 'micro_duration_ms_min', min: 1, max: 1000, defaultValue: 5, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX', name: 'micro_duration_ms_max', min: 1, max: 1000, defaultValue: 15, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN', name: 'short_duration_ms_min', min: 1, max: 2000, defaultValue: 20, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX', name: 'short_duration_ms_max', min: 1, max: 2000, defaultValue: 50, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN', name: 'body_duration_ms_min', min: 1, max: 5000, defaultValue: 80, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX', name: 'body_duration_ms_max', min: 1, max: 5000, defaultValue: 200, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_RNG_SEED', name: 'rng_seed', min: 1, max: 2147483520, defaultValue: 1, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN', name: 'mix_dry_gain', min: 0, max: 2, defaultValue: 1, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MIX_WET_GAIN', name: 'mix_wet_gain', min: 0, max: 2, defaultValue: 1, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_STEREO_WIDTH', name: 'stereo_width', min: 0, max: 1, defaultValue: 0.7, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD', name: 'attack_pan_spread', min: 0, max: 1, defaultValue: 0.85, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD', name: 'sustain_pan_spread', min: 0, max: 1, defaultValue: 0.45, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SMART_START_ENABLE', name: 'smart_start_enable', min: 0, max: 1, defaultValue: 1, type: 'bool' },
    { id: 'BUBBLE_ENGINE_PARAM_SMART_START_RANGE', name: 'smart_start_range', min: 0, max: 256, defaultValue: 12, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION', name: 'envelope_variation', min: 0, max: 1, defaultValue: 0.35, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY', name: 'envelope_family', min: 0, max: 1, defaultValue: 0, type: 'enum' },
    { id: 'BUBBLE_ENGINE_PARAM_WET_DRIVE', name: 'wet_drive', min: 0, max: 4, defaultValue: 1, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT', name: 'wet_clip_amount', min: 0, max: 1, defaultValue: 0.2, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM', name: 'wet_output_trim', min: 0, max: 2, defaultValue: 1, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE', name: 'sustain_diffusion_enable', min: 0, max: 1, defaultValue: 0, type: 'bool' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT', name: 'sustain_diffusion_amount', min: 0, max: 1, defaultValue: 0.35, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES', name: 'sustain_diffusion_stages', min: 0, max: 2, defaultValue: 1, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY', name: 'sustain_diffusion_delay', min: 1, max: 96, defaultValue: 18, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK', name: 'sustain_diffusion_feedback', min: 0, max: 0.95, defaultValue: 0.45, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DROPLET_ENABLE', name: 'droplet_enable', min: 0, max: 1, defaultValue: 0, type: 'bool' },
    { id: 'BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY', name: 'droplet_probability', min: 0, max: 1, defaultValue: 0.12, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DROPLET_GAIN', name: 'droplet_gain', min: 0, max: 2, defaultValue: 0.5, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE', name: 'droplet_length_scale', min: 0.05, max: 4, defaultValue: 0.6, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MEMORY_MIX', name: 'memory_mix', min: 0, max: 1, defaultValue: 0.35, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MEMORY_PULL', name: 'memory_pull', min: 0, max: 1, defaultValue: 0.25, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MEMORY_DARKENING', name: 'memory_darkening', min: 0, max: 1, defaultValue: 0.2, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_TONE_VARIATION', name: 'tone_variation', min: 0, max: 1, defaultValue: 0.4, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS', name: 'attack_brightness', min: 0, max: 4, defaultValue: 1.15, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS', name: 'sustain_darkness', min: 0, max: 1, defaultValue: 0.25, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER', name: 'attack_rate_jitter', min: 0, max: 1, defaultValue: 0, type: 'bool' },
    { id: 'BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH', name: 'attack_rate_jitter_depth', min: 0, max: 1, defaultValue: 0.02, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_QUALITY_PROFILE', name: 'quality_profile', min: 0, max: 3, defaultValue: 2, type: 'enum' },
    { id: 'BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT', name: 'active_voice_limit', min: 1, max: 32, defaultValue: 24, type: 'int' },
    { id: 'BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT', name: 'freeze_amount', min: 0, max: 1, defaultValue: 0, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_FREEZE_ENABLED', name: 'freeze_enabled', min: 0, max: 1, defaultValue: 0, type: 'bool' },
    { id: 'BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY', name: 'reverse_probability', min: 0, max: 1, defaultValue: 0, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_PITCH_MODE', name: 'pitch_mode', min: 0, max: 4, defaultValue: 0, type: 'enum' },
    { id: 'BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT', name: 'shimmer_amount', min: 0, max: 1, defaultValue: 0, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB', name: 'final_limiter_ceiling_db', min: -24, max: 0, defaultValue: -1, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS', name: 'final_limiter_release_ms', min: 5, max: 500, defaultValue: 50, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MOTION_RATE', name: 'motion_rate', min: 0, max: 1, defaultValue: 0.18, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MOTION_DEPTH', name: 'motion_depth', min: 0, max: 1, defaultValue: 0, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_MOTION_SHAPE', name: 'motion_shape', min: 0, max: 2, defaultValue: 0, type: 'enum' },
    { id: 'BUBBLE_ENGINE_PARAM_TEMPO_BPM', name: 'tempo_bpm', min: 20, max: 300, defaultValue: 120, type: 'float' },
    { id: 'BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED', name: 'tempo_sync_enabled', min: 0, max: 1, defaultValue: 0, type: 'bool' },
    { id: 'BUBBLE_ENGINE_PARAM_RHYTHM_DIVISION', name: 'rhythm_division', min: 0, max: 3, defaultValue: 2, type: 'enum' },
    { id: 'BUBBLE_ENGINE_PARAM_BURST_MODE', name: 'burst_mode', min: 0, max: 4, defaultValue: 0, type: 'enum' },
    { id: 'BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN', name: 'rhythm_pattern', min: 0, max: 4294967295, defaultValue: 4369, type: 'int' },
  ];

  const PARAM_BY_NAME = Object.freeze(Object.fromEntries(PARAM_SPECS.map((spec) => [spec.name, Object.freeze({ ...spec })])));
  const DEFAULT_PARAMS = Object.freeze(Object.fromEntries(PARAM_SPECS.map((spec) => [spec.name, spec.defaultValue])));

  function slugify(value) {
    return String(value || '')
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/(^-|-$)/g, '') || 'preset';
  }

  function normalizeParamValue(spec, value) {
    if (spec.type === 'int' || spec.type === 'bool' || spec.type === 'enum') {
      return Math.trunc(value);
    }
    return value;
  }

  function validateParam(name, value) {
    const spec = PARAM_BY_NAME[name];
    if (!spec) return { valid: false, reason: `Unknown preset param: ${name}` };
    const num = Number(value);
    if (!Number.isFinite(num)) {
      return { valid: false, reason: `${name} must be a valid finite number` };
    }
    const normalized = normalizeParamValue(spec, num);
    if (normalized < spec.min || normalized > spec.max) {
      return { valid: false, reason: `${name} must be between ${spec.min} and ${spec.max}` };
    }
    return { valid: true, value: normalized };
  }

  function createDefaultParams() {
    return { ...DEFAULT_PARAMS };
  }

  function normalizeParams(inputParams) {
    const params = createDefaultParams();
    if (!inputParams || typeof inputParams !== 'object') return params;
    for (const [name, rawValue] of Object.entries(inputParams)) {
      if (!PARAM_BY_NAME[name]) {
        params[name] = rawValue;
        continue;
      }
      const result = validateParam(name, rawValue);
      if (result.valid) params[name] = result.value;
    }
    return params;
  }

  function createCanonicalPreset(input = {}) {
    const nowIso = new Date().toISOString();
    const params = normalizeParams(input.params || {});
    return {
      schema_version: SCHEMA_VERSION,
      preset_name: input.preset_name || 'Untitled Preset',
      preset_slug: input.preset_slug || slugify(input.preset_name),
      engine_version: input.engine_version || ENGINE_VERSION,
      created_at: input.created_at || nowIso,
      ui_category: input.ui_category || DEFAULT_CATEGORY,
      tags: Array.isArray(input.tags) ? input.tags : [],
      esp32_safe: Boolean(input.esp32_safe),
      quality_tier: input.quality_tier || DEFAULT_QUALITY_TIER,
      description: input.description || '',
      params,
      base_params: input.base_params && typeof input.base_params === 'object' ? normalizeParams(input.base_params) : { ...params },
      macro_values: input.macro_values && typeof input.macro_values === 'object' ? { ...input.macro_values } : {},
      metadata: input.metadata && typeof input.metadata === 'object' ? { ...input.metadata } : {},
    };
  }

  global.BubbleCloudPresetSchema = {
    SCHEMA_VERSION,
    ENGINE_VERSION,
    DEFAULT_CATEGORY,
    DEFAULT_QUALITY_TIER,
    PARAM_SPECS: Object.freeze(PARAM_SPECS.map((spec) => Object.freeze({ ...spec }))),
    PARAM_BY_NAME,
    DEFAULT_PARAMS,
    createCanonicalPreset,
    createDefaultParams,
    normalizeParams,
    validateParam,
    slugify,
  };
})(window);
