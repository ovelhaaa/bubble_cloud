(function initPresetSchema(global) {
  const SCHEMA_VERSION = 3;
  const ENGINE_VERSION = 'post-diffusion-ui';
  const DEFAULT_CATEGORY = 'Utility';
  const DEFAULT_QUALITY_TIER = 'ESP32_SAFE';

  function slugify(value) {
    return String(value || '')
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/(^-|-$)/g, '') || 'preset';
  }

  function createCanonicalPreset(input) {
    const nowIso = new Date().toISOString();
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
      params: { ...(input.params || {}) },
      metadata: input.metadata && typeof input.metadata === 'object' ? { ...input.metadata } : {},
    };
  }

  global.BubbleCloudPresetSchema = {
    SCHEMA_VERSION,
    ENGINE_VERSION,
    DEFAULT_CATEGORY,
    DEFAULT_QUALITY_TIER,
    createCanonicalPreset,
    slugify,
  };
})(window);
