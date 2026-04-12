(function initFactoryPresets(global) {
  function buildPreset(base, config) {
    return {
      ...config,
      params: {
        ...base,
        ...config.params,
      },
    };
  }

  function createFactoryPresets(baseParams) {
    return [
      buildPreset(baseParams, {
        preset_name: 'Glass Bloom',
        preset_slug: 'glass-bloom',
        description: 'Brilho aberto com ataque vivo e sustain limpo levemente difuso.',
        ui_category: 'Expressive',
        tags: ['bright', 'stereo', 'attack', 'shimmer', 'esp32-safe'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { stereo_width: 0.88, attack_pan_spread: 0.95, sustain_pan_spread: 0.56, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.28, sustain_diffusion_stages: 1, sustain_diffusion_feedback: 0.35, attack_brightness: 1.38, sustain_darkness: 0.18, density_burst: 72, density_sustain: 20, droplet_enable: 0, memory_mix: 0.25, tone_variation: 0.46, rng_seed: 1401 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Soft Halo',
        preset_slug: 'soft-halo',
        description: 'Nuvem macia e arredondada com ataques suaves para acordes limpos.',
        ui_category: 'Ambient',
        tags: ['soft', 'diffuse', 'stereo', 'pad', 'esp32-safe'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { attack_brightness: 0.95, sustain_darkness: 0.34, density_burst: 38, density_sustain: 24, burst_immediate_count: 2, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.44, sustain_diffusion_stages: 1, sustain_diffusion_delay: 22, sustain_diffusion_feedback: 0.42, stereo_width: 0.78, attack_pan_spread: 0.64, sustain_pan_spread: 0.62, memory_mix: 0.38, memory_pull: 0.3, rng_seed: 2212 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Memory Pad',
        preset_slug: 'memory-pad',
        description: 'Sustain longo com peso de memória, escuro e atmosférico.',
        ui_category: 'Ambient',
        tags: ['memory', 'pad', 'dense', 'soft'],
        esp32_safe: false,
        quality_tier: 'HIGH_QUALITY',
        params: { density_burst: 26, density_sustain: 33, density_decay: 2, body_duration_ms_min: 120, body_duration_ms_max: 310, short_duration_ms_max: 72, memory_mix: 0.6, memory_pull: 0.56, memory_darkening: 0.48, sustain_darkness: 0.52, attack_brightness: 0.82, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.58, sustain_diffusion_stages: 2, sustain_diffusion_feedback: 0.56, droplet_enable: 1, droplet_probability: 0.16, droplet_gain: 0.42, stereo_width: 0.68, rng_seed: 3107 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Shimmer Dust',
        preset_slug: 'shimmer-dust',
        description: 'Granular cintilante com estéreo amplo, jitter leve e droplets.',
        ui_category: 'Expressive',
        tags: ['shimmer', 'bright', 'stereo', 'diffuse'],
        esp32_safe: false,
        quality_tier: 'HIGH_QUALITY',
        params: { density_burst: 68, density_sustain: 28, burst_immediate_count: 4, stereo_width: 0.94, attack_pan_spread: 0.96, sustain_pan_spread: 0.71, droplet_enable: 1, droplet_probability: 0.22, droplet_gain: 0.55, droplet_length_scale: 0.5, attack_rate_jitter: 1, attack_rate_jitter_depth: 0.055, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.41, attack_brightness: 1.46, tone_variation: 0.62, rng_seed: 4209 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Frozen Attack',
        preset_slug: 'frozen-attack',
        description: 'Fragmentação de ataque em foco com sustain seco e resposta percussiva.',
        ui_category: 'Expressive',
        tags: ['attack', 'bright', 'dense', 'esp32-safe'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { burst_duration_ticks: 6, burst_immediate_count: 5, density_burst: 95, density_sustain: 10, density_decay: 8, attack_region_min_offset_samples: 220, attack_region_max_offset_samples: 2200, body_region_min_offset_samples: 2600, body_region_max_offset_samples: 7600, sustain_diffusion_enable: 0, droplet_enable: 0, memory_mix: 0.18, memory_pull: 0.12, attack_brightness: 1.55, sustain_darkness: 0.2, transient_delta: 0.07, rng_seed: 5173 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Smoky Chorus',
        preset_slug: 'smoky-chorus',
        description: 'Pseudo-chorus granular escuro e difuso para textura base.',
        ui_category: 'Utility',
        tags: ['soft', 'dense', 'stereo', 'diffuse', 'pad'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { density_burst: 30, density_sustain: 26, density_decay: 3, attack_pan_spread: 0.58, sustain_pan_spread: 0.72, stereo_width: 0.74, envelope_variation: 0.56, tone_variation: 0.35, attack_brightness: 0.78, sustain_darkness: 0.58, memory_mix: 0.45, memory_darkening: 0.5, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.34, sustain_diffusion_feedback: 0.48, attack_rate_jitter: 1, attack_rate_jitter_depth: 0.026, rng_seed: 6121 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Tape Cathedral',
        preset_slug: 'tape-cathedral',
        description: 'Ambiência ampla e quente com cauda longa, ótima para swells e worship.',
        ui_category: 'Ambient',
        tags: ['ambient', 'warm', 'wide', 'swells', 'cathedral'],
        esp32_safe: false,
        quality_tier: 'HIGH_QUALITY',
        params: { density_burst: 24, density_sustain: 36, density_decay: 2, burst_immediate_count: 2, attack_brightness: 0.76, sustain_darkness: 0.62, stereo_width: 0.92, attack_pan_spread: 0.62, sustain_pan_spread: 0.86, memory_mix: 0.68, memory_pull: 0.58, memory_darkening: 0.54, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.64, sustain_diffusion_stages: 2, sustain_diffusion_feedback: 0.58, body_duration_ms_min: 140, body_duration_ms_max: 320, rng_seed: 7319 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Neon Funk Grid',
        preset_slug: 'neon-funk-grid',
        description: 'Ataque recortado e brilhante com decay curto para riffs funk e disco modernos.',
        ui_category: 'Expressive',
        tags: ['funk', 'tight', 'bright', 'rhythmic', 'esp32-safe'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { burst_duration_ticks: 5, burst_immediate_count: 5, density_burst: 90, density_sustain: 8, density_decay: 8, attack_brightness: 1.62, sustain_darkness: 0.16, attack_region_min_offset_samples: 160, attack_region_max_offset_samples: 1850, body_region_min_offset_samples: 2400, body_region_max_offset_samples: 6400, transient_delta: 0.09, stereo_width: 0.84, attack_pan_spread: 0.88, sustain_pan_spread: 0.42, sustain_diffusion_enable: 0, droplet_enable: 0, memory_mix: 0.12, rng_seed: 8426 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Cinematic Reverse Bloom',
        preset_slug: 'cinematic-reverse-bloom',
        description: 'Envelope mais lento e memória alta para sensação de swell reverso cinematográfico.',
        ui_category: 'Ambient',
        tags: ['cinematic', 'reverse-feel', 'swell', 'pad', 'diffuse'],
        esp32_safe: false,
        quality_tier: 'HIGH_QUALITY',
        params: { density_burst: 20, density_sustain: 34, density_decay: 2, burst_immediate_count: 1, attack_brightness: 0.84, sustain_darkness: 0.48, envelope_variation: 0.64, memory_mix: 0.72, memory_pull: 0.66, memory_darkening: 0.42, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.6, sustain_diffusion_stages: 2, sustain_diffusion_feedback: 0.52, sustain_diffusion_delay: 28, stereo_width: 0.9, attack_pan_spread: 0.52, sustain_pan_spread: 0.84, droplet_enable: 1, droplet_probability: 0.1, droplet_gain: 0.34, rng_seed: 9532 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Lo-Fi Velvet Cloud',
        preset_slug: 'lo-fi-velvet-cloud',
        description: 'Textura aveludada com escurecimento extra, perfeita para neo-soul e lo-fi.',
        ui_category: 'Utility',
        tags: ['lofi', 'dark', 'soft', 'neo-soul', 'esp32-safe'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { density_burst: 34, density_sustain: 24, density_decay: 3, attack_brightness: 0.7, sustain_darkness: 0.72, memory_mix: 0.52, memory_pull: 0.4, memory_darkening: 0.62, sustain_diffusion_enable: 1, sustain_diffusion_amount: 0.3, sustain_diffusion_feedback: 0.4, stereo_width: 0.66, attack_pan_spread: 0.48, sustain_pan_spread: 0.58, tone_variation: 0.28, envelope_variation: 0.42, droplet_enable: 0, rng_seed: 1054 }
      }),
      buildPreset(baseParams, {
        preset_name: 'Math Spark Delayless',
        preset_slug: 'math-spark-delayless',
        description: 'Resposta hiper-articulada e estéreo controlado para linhas rápidas com definição.',
        ui_category: 'Expressive',
        tags: ['math-rock', 'articulate', 'tight', 'stereo', 'esp32-safe'],
        esp32_safe: true,
        quality_tier: 'ESP32_SAFE',
        params: { burst_duration_ticks: 4, burst_immediate_count: 4, density_burst: 82, density_sustain: 12, density_decay: 7, attack_brightness: 1.48, sustain_darkness: 0.22, attack_region_min_offset_samples: 120, attack_region_max_offset_samples: 1300, body_region_min_offset_samples: 2100, body_region_max_offset_samples: 5600, transient_delta: 0.08, stereo_width: 0.8, attack_pan_spread: 0.74, sustain_pan_spread: 0.36, sustain_diffusion_enable: 0, attack_rate_jitter: 1, attack_rate_jitter_depth: 0.02, memory_mix: 0.16, droplet_enable: 0, rng_seed: 1168 }
      })
    ];
  }

  global.BubbleCloudFactoryPresets = { createFactoryPresets };
})(window);
