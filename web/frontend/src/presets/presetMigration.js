(function initPresetMigration(global) {
  const LEGACY_PARAM_MAP = {
    master_dry_gain: 'mix_dry_gain',
    master_wet_gain: 'mix_wet_gain',
  };

  function cloneObject(obj) {
    return JSON.parse(JSON.stringify(obj || {}));
  }

  function migrateLegacyToCanonical(input, schemaVersion, defaults, slugify) {
    const raw = cloneObject(input);
    const incomingParams = raw.params && typeof raw.params === 'object' ? raw.params : raw;
    const migratedParams = { ...defaults };

    Object.keys(incomingParams || {}).forEach((key) => {
      const mapped = LEGACY_PARAM_MAP[key] || key;
      migratedParams[mapped] = incomingParams[key];
    });

    return {
      schema_version: schemaVersion,
      preset_name: raw.preset_name || raw.name || 'Imported Legacy Preset',
      preset_slug: raw.preset_slug || slugify(raw.preset_name || raw.name || 'imported-legacy-preset'),
      engine_version: raw.engine_version || 'legacy',
      created_at: raw.created_at || new Date().toISOString(),
      ui_category: raw.ui_category || 'Utility',
      tags: Array.isArray(raw.tags) ? raw.tags : ['legacy-import'],
      esp32_safe: typeof raw.esp32_safe === 'boolean' ? raw.esp32_safe : true,
      quality_tier: raw.quality_tier || 'ESP32_SAFE',
      description: raw.description || 'Preset legado convertido automaticamente.',
      params: migratedParams,
      metadata: {
        ...(raw.metadata && typeof raw.metadata === 'object' ? raw.metadata : {}),
        migrated_from_legacy: true,
        original_schema_version: raw.schema_version || null,
      },
    };
  }

  global.BubbleCloudPresetMigration = { migrateLegacyToCanonical };
})(window);
