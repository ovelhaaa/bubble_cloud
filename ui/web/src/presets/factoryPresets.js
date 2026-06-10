(function initFactoryPresets(global) {
  const CANONICAL_FACTORY_PRESETS = [
    {
        schema_version: 3,
        preset_name: "Ambient Bloom",
        preset_slug: "ambient-bloom",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "ambient",
            "bloom",
            "pad",
            "wide",
            "mcu-plus"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Camada expansiva e macia para acordes lentos, com bloom largo e cauda que envolve sem apagar o ataque.",
        params: {
            "burst_immediate_count": 2,
            "density_burst": 26,
            "density_sustain": 28,
            "density_decay": 2,
            "body_duration_ms_min": 150,
            "body_duration_ms_max": 360,
            "rng_seed": 1001,
            "stereo_width": 0.9,
            "sustain_pan_spread": 0.82,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.58,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.54,
            "memory_mix": 0.62,
            "memory_pull": 0.52,
            "memory_darkening": 0.44,
            "attack_brightness": 0.82,
            "sustain_darkness": 0.56,
            "quality_profile": 1,
            "active_voice_limit": 16,
            "freeze_amount": 0.34,
            "freeze_enabled": 1,
            "motion_rate": 0.16,
            "motion_depth": 0.18
        },
        macro_values: {
            "density": 0.44,
            "bloom": 0.86,
            "motion": 0.34,
            "texture": 0.48,
            "space": 0.78,
            "gravity": 0.58,
            "memory": 0.7,
            "clarity": 0.36,
            "freeze": 0.52,
            "sparkle": 0.28,
            "warmth": 0.74,
            "mix": 0.58
        },
        metadata: {
            "recommended_min_profile": "MCU_PLUS",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_PLUS",
                "active_voice_limit": 16
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.34
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.16,
                    "depth": 0.18,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Frozen Cathedral",
        preset_slug: "frozen-cathedral",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "cathedral",
            "frozen",
            "dark",
            "swell",
            "web-standard"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Órgão granular congelado, escuro e monumental, feito para swells e notas sustentadas em espaços enormes.",
        params: {
            "density_burst": 20,
            "density_sustain": 34,
            "density_decay": 1.5,
            "body_duration_ms_min": 180,
            "body_duration_ms_max": 460,
            "rng_seed": 1002,
            "stereo_width": 0.96,
            "sustain_pan_spread": 0.9,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.76,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.66,
            "memory_mix": 0.78,
            "memory_pull": 0.68,
            "memory_darkening": 0.62,
            "attack_brightness": 0.64,
            "sustain_darkness": 0.74,
            "quality_profile": 2,
            "active_voice_limit": 24,
            "freeze_amount": 0.82,
            "freeze_enabled": 1,
            "reverse_probability": 0.08,
            "motion_rate": 0.1,
            "motion_depth": 0.1
        },
        macro_values: {
            "density": 0.5,
            "bloom": 0.92,
            "motion": 0.22,
            "texture": 0.42,
            "space": 0.92,
            "gravity": 0.72,
            "memory": 0.86,
            "clarity": 0.24,
            "freeze": 0.88,
            "sparkle": 0.22,
            "warmth": 0.62,
            "mix": 0.64
        },
        metadata: {
            "recommended_min_profile": "WEB_STANDARD",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_STANDARD",
                "active_voice_limit": 24
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.82
                },
                "reverse": {
                    "probability": 0.08
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.1,
                    "depth": 0.1,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Glass Rain",
        preset_slug: "glass-rain",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Expressive",
        tags: [
            "glass",
            "rain",
            "droplets",
            "bright",
            "stereo"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Gotículas brilhantes caindo em estéreo, com reversos raros e shimmer leve para arpejos limpos.",
        params: {
            "burst_immediate_count": 4,
            "density_burst": 62,
            "density_sustain": 24,
            "density_decay": 4,
            "micro_duration_ms_min": 3,
            "micro_duration_ms_max": 18,
            "short_duration_ms_max": 70,
            "rng_seed": 1003,
            "stereo_width": 0.94,
            "attack_pan_spread": 0.94,
            "sustain_pan_spread": 0.72,
            "droplet_enable": 1,
            "droplet_probability": 0.28,
            "droplet_gain": 0.55,
            "droplet_length_scale": 0.38,
            "tone_variation": 0.68,
            "attack_brightness": 1.62,
            "sustain_darkness": 0.18,
            "attack_rate_jitter": 1,
            "attack_rate_jitter_depth": 0.055,
            "quality_profile": 1,
            "active_voice_limit": 16,
            "reverse_probability": 0.12,
            "pitch_mode": 1,
            "shimmer_amount": 0.42,
            "motion_rate": 0.44,
            "motion_depth": 0.45,
            "motion_shape": 1
        },
        macro_values: {
            "density": 0.62,
            "bloom": 0.54,
            "motion": 0.68,
            "texture": 0.72,
            "space": 0.74,
            "gravity": 0.36,
            "memory": 0.42,
            "clarity": 0.74,
            "freeze": 0.18,
            "sparkle": 0.82,
            "warmth": 0.28,
            "mix": 0.6
        },
        metadata: {
            "recommended_min_profile": "MCU_PLUS",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_PLUS",
                "active_voice_limit": 16
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.12
                },
                "pitch": {
                    "mode": 1,
                    "shimmer_amount": 0.42
                },
                "motion": {
                    "rate": 0.44,
                    "depth": 0.45,
                    "shape": 1
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Shoegaze Cloud",
        preset_slug: "shoegaze-cloud",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "shoegaze",
            "cloud",
            "warm",
            "diffuse",
            "wall"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Muralha difusa e saturada, quente e enevoada, para guitarras dream-pop/shoegaze com ataque dissolvido.",
        params: {
            "burst_immediate_count": 2,
            "density_burst": 36,
            "density_sustain": 44,
            "density_decay": 2,
            "body_duration_ms_min": 170,
            "body_duration_ms_max": 430,
            "rng_seed": 1004,
            "stereo_width": 0.98,
            "attack_pan_spread": 0.7,
            "sustain_pan_spread": 0.92,
            "wet_drive": 1.35,
            "wet_clip_amount": 0.3,
            "wet_output_trim": 0.9,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.72,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.62,
            "memory_mix": 0.74,
            "memory_pull": 0.62,
            "memory_darkening": 0.56,
            "attack_brightness": 0.72,
            "sustain_darkness": 0.68,
            "quality_profile": 2,
            "active_voice_limit": 24,
            "freeze_amount": 0.44,
            "freeze_enabled": 1,
            "motion_rate": 0.2,
            "motion_depth": 0.24
        },
        macro_values: {
            "density": 0.7,
            "bloom": 0.88,
            "motion": 0.4,
            "texture": 0.64,
            "space": 0.86,
            "gravity": 0.66,
            "memory": 0.8,
            "clarity": 0.2,
            "freeze": 0.58,
            "sparkle": 0.34,
            "warmth": 0.86,
            "mix": 0.72
        },
        metadata: {
            "recommended_min_profile": "WEB_STANDARD",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_STANDARD",
                "active_voice_limit": 24
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.44
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.2,
                    "depth": 0.24,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Dream Pad",
        preset_slug: "dream-pad",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "dream",
            "pad",
            "soft",
            "warm",
            "mcu-safe"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Pad aveludado e harmônico para bases longas, com movimento lento e brilho controlado.",
        params: {
            "burst_immediate_count": 2,
            "density_burst": 22,
            "density_sustain": 22,
            "density_decay": 2.5,
            "body_duration_ms_min": 140,
            "body_duration_ms_max": 320,
            "rng_seed": 1005,
            "stereo_width": 0.76,
            "sustain_pan_spread": 0.62,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.42,
            "sustain_diffusion_stages": 1,
            "sustain_diffusion_feedback": 0.44,
            "memory_mix": 0.58,
            "memory_pull": 0.46,
            "memory_darkening": 0.48,
            "attack_brightness": 0.76,
            "sustain_darkness": 0.6,
            "quality_profile": 0,
            "active_voice_limit": 8,
            "freeze_amount": 0.28,
            "freeze_enabled": 1,
            "motion_rate": 0.14,
            "motion_depth": 0.16
        },
        macro_values: {
            "density": 0.38,
            "bloom": 0.8,
            "motion": 0.3,
            "texture": 0.38,
            "space": 0.68,
            "gravity": 0.6,
            "memory": 0.68,
            "clarity": 0.4,
            "freeze": 0.48,
            "sparkle": 0.3,
            "warmth": 0.82,
            "mix": 0.55
        },
        metadata: {
            "recommended_min_profile": "MCU_SAFE",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_SAFE",
                "active_voice_limit": 8
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.28
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.14,
                    "depth": 0.16,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Ocean Mist",
        preset_slug: "ocean-mist",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "ocean",
            "mist",
            "fluid",
            "dark",
            "mcu-plus"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Névoa estéreo fluida e respirante, escura o suficiente para funcionar atrás de melodias.",
        params: {
            "density_burst": 28,
            "density_sustain": 30,
            "density_decay": 2.2,
            "body_duration_ms_min": 130,
            "body_duration_ms_max": 340,
            "rng_seed": 1006,
            "stereo_width": 0.88,
            "sustain_pan_spread": 0.78,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.54,
            "sustain_diffusion_stages": 1,
            "sustain_diffusion_feedback": 0.5,
            "memory_mix": 0.58,
            "memory_pull": 0.5,
            "memory_darkening": 0.6,
            "attack_brightness": 0.7,
            "sustain_darkness": 0.7,
            "quality_profile": 1,
            "active_voice_limit": 16,
            "freeze_amount": 0.24,
            "freeze_enabled": 1,
            "reverse_probability": 0.05,
            "motion_rate": 0.24,
            "motion_depth": 0.34,
            "motion_shape": 0
        },
        macro_values: {
            "density": 0.46,
            "bloom": 0.76,
            "motion": 0.52,
            "texture": 0.44,
            "space": 0.82,
            "gravity": 0.64,
            "memory": 0.64,
            "clarity": 0.34,
            "freeze": 0.44,
            "sparkle": 0.24,
            "warmth": 0.58,
            "mix": 0.57
        },
        metadata: {
            "recommended_min_profile": "MCU_PLUS",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_PLUS",
                "active_voice_limit": 16
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.24
                },
                "reverse": {
                    "probability": 0.05
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.24,
                    "depth": 0.34,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Submerged Piano",
        preset_slug: "submerged-piano",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Utility",
        tags: [
            "piano",
            "submerged",
            "dark",
            "memory",
            "mcu-safe"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Reflexos abafados e submersos para piano ou clean guitar, preservando transientes com corpo escuro.",
        params: {
            "burst_immediate_count": 2,
            "density_burst": 30,
            "density_sustain": 18,
            "density_decay": 3,
            "body_duration_ms_min": 110,
            "body_duration_ms_max": 260,
            "rng_seed": 1007,
            "stereo_width": 0.64,
            "attack_pan_spread": 0.44,
            "sustain_pan_spread": 0.56,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.28,
            "sustain_diffusion_stages": 1,
            "sustain_diffusion_feedback": 0.38,
            "memory_mix": 0.62,
            "memory_pull": 0.58,
            "memory_darkening": 0.68,
            "attack_brightness": 0.68,
            "sustain_darkness": 0.78,
            "quality_profile": 0,
            "active_voice_limit": 8,
            "motion_rate": 0.12,
            "motion_depth": 0.1
        },
        macro_values: {
            "density": 0.34,
            "bloom": 0.62,
            "motion": 0.28,
            "texture": 0.36,
            "space": 0.6,
            "gravity": 0.76,
            "memory": 0.72,
            "clarity": 0.46,
            "freeze": 0.3,
            "sparkle": 0.18,
            "warmth": 0.64,
            "mix": 0.5
        },
        metadata: {
            "recommended_min_profile": "MCU_SAFE",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_SAFE",
                "active_voice_limit": 8
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.12,
                    "depth": 0.1,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Reverse Horizon",
        preset_slug: "reverse-horizon",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Expressive",
        tags: [
            "reverse",
            "swell",
            "horizon",
            "motion",
            "web-standard"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Swells reversos que puxam a frase para trás antes de abrir no horizonte estéreo.",
        params: {
            "burst_immediate_count": 2,
            "density_burst": 32,
            "density_sustain": 34,
            "density_decay": 2.8,
            "body_duration_ms_min": 160,
            "body_duration_ms_max": 420,
            "rng_seed": 1008,
            "stereo_width": 0.96,
            "attack_pan_spread": 0.82,
            "sustain_pan_spread": 0.88,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.6,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.52,
            "memory_mix": 0.62,
            "memory_pull": 0.54,
            "attack_brightness": 0.98,
            "sustain_darkness": 0.42,
            "quality_profile": 2,
            "active_voice_limit": 24,
            "reverse_probability": 0.48,
            "pitch_mode": 1,
            "shimmer_amount": 0.28,
            "motion_rate": 0.42,
            "motion_depth": 0.62,
            "motion_shape": 1
        },
        macro_values: {
            "density": 0.56,
            "bloom": 0.72,
            "motion": 0.78,
            "texture": 0.58,
            "space": 0.88,
            "gravity": 0.44,
            "memory": 0.66,
            "clarity": 0.42,
            "freeze": 0.36,
            "sparkle": 0.5,
            "warmth": 0.46,
            "mix": 0.68
        },
        metadata: {
            "recommended_min_profile": "WEB_STANDARD",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_STANDARD",
                "active_voice_limit": 24
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.48
                },
                "pitch": {
                    "mode": 1,
                    "shimmer_amount": 0.28
                },
                "motion": {
                    "rate": 0.42,
                    "depth": 0.62,
                    "shape": 1
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Infinite Guitar",
        preset_slug: "infinite-guitar",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "infinite",
            "guitar",
            "freeze",
            "sustain",
            "web-standard"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Sustain quase infinito para notas e ebows, com freeze musical e memória longa sem mascarar totalmente o dry.",
        params: {
            "burst_immediate_count": 1,
            "density_burst": 18,
            "density_sustain": 38,
            "density_decay": 1,
            "body_duration_ms_min": 200,
            "body_duration_ms_max": 520,
            "rng_seed": 1009,
            "stereo_width": 0.9,
            "sustain_pan_spread": 0.8,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.7,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.7,
            "memory_mix": 0.86,
            "memory_pull": 0.78,
            "memory_darkening": 0.48,
            "attack_brightness": 0.78,
            "sustain_darkness": 0.58,
            "quality_profile": 2,
            "active_voice_limit": 24,
            "freeze_amount": 0.96,
            "freeze_enabled": 1,
            "motion_rate": 0.1,
            "motion_depth": 0.12
        },
        macro_values: {
            "density": 0.42,
            "bloom": 0.9,
            "motion": 0.24,
            "texture": 0.4,
            "space": 0.8,
            "gravity": 0.7,
            "memory": 0.9,
            "clarity": 0.32,
            "freeze": 0.96,
            "sparkle": 0.32,
            "warmth": 0.7,
            "mix": 0.62
        },
        metadata: {
            "recommended_min_profile": "WEB_STANDARD",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_STANDARD",
                "active_voice_limit": 24
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.96
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.1,
                    "depth": 0.12,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Dust Particles",
        preset_slug: "dust-particles",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Expressive",
        tags: [
            "dust",
            "particles",
            "rhythmic",
            "bright",
            "mcu-safe"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Partículas curtas, secas e cintilantes para detalhes rítmicos, preservando muito da articulação.",
        params: {
            "burst_duration_ticks": 4,
            "burst_immediate_count": 5,
            "density_burst": 76,
            "density_sustain": 10,
            "density_decay": 8,
            "attack_region_min_offset_samples": 80,
            "attack_region_max_offset_samples": 1500,
            "body_region_min_offset_samples": 1800,
            "body_region_max_offset_samples": 5600,
            "micro_duration_ms_min": 2,
            "micro_duration_ms_max": 10,
            "short_duration_ms_min": 12,
            "short_duration_ms_max": 38,
            "rng_seed": 1010,
            "stereo_width": 0.7,
            "attack_pan_spread": 0.78,
            "sustain_pan_spread": 0.34,
            "droplet_enable": 1,
            "droplet_probability": 0.18,
            "droplet_gain": 0.38,
            "droplet_length_scale": 0.28,
            "attack_brightness": 1.5,
            "sustain_darkness": 0.2,
            "attack_rate_jitter": 1,
            "attack_rate_jitter_depth": 0.04,
            "quality_profile": 0,
            "active_voice_limit": 8,
            "motion_rate": 0.48,
            "motion_depth": 0.38
        },
        macro_values: {
            "density": 0.58,
            "bloom": 0.34,
            "motion": 0.64,
            "texture": 0.82,
            "space": 0.48,
            "gravity": 0.28,
            "memory": 0.24,
            "clarity": 0.82,
            "freeze": 0.08,
            "sparkle": 0.72,
            "warmth": 0.34,
            "mix": 0.46
        },
        metadata: {
            "recommended_min_profile": "MCU_SAFE",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_SAFE",
                "active_voice_limit": 8
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.48,
                    "depth": 0.38,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Ghost Chorus",
        preset_slug: "ghost-chorus",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Utility",
        tags: [
            "ghost",
            "chorus",
            "wide",
            "soft",
            "mcu-plus"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Pseudo-chorus fantasmagórico, estéreo e levemente desafinado para bases limpas e vozes processadas.",
        params: {
            "density_burst": 32,
            "density_sustain": 28,
            "density_decay": 3.2,
            "rng_seed": 1011,
            "stereo_width": 0.92,
            "attack_pan_spread": 0.64,
            "sustain_pan_spread": 0.84,
            "envelope_variation": 0.62,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.44,
            "sustain_diffusion_stages": 1,
            "sustain_diffusion_feedback": 0.48,
            "memory_mix": 0.5,
            "memory_pull": 0.4,
            "tone_variation": 0.58,
            "attack_brightness": 0.88,
            "sustain_darkness": 0.48,
            "quality_profile": 1,
            "active_voice_limit": 16,
            "pitch_mode": 2,
            "motion_rate": 0.32,
            "motion_depth": 0.46,
            "motion_shape": 0
        },
        macro_values: {
            "density": 0.48,
            "bloom": 0.66,
            "motion": 0.56,
            "texture": 0.46,
            "space": 0.72,
            "gravity": 0.52,
            "memory": 0.58,
            "clarity": 0.44,
            "freeze": 0.26,
            "sparkle": 0.4,
            "warmth": 0.52,
            "mix": 0.54
        },
        metadata: {
            "recommended_min_profile": "MCU_PLUS",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_PLUS",
                "active_voice_limit": 16
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 2,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.32,
                    "depth": 0.46,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Crystal Bloom",
        preset_slug: "crystal-bloom",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Expressive",
        tags: [
            "crystal",
            "bloom",
            "shimmer",
            "bright",
            "web-standard"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Bloom claro e cristalino com shimmer audível, ideal para harmônicos, arpejos lentos e aberturas.",
        params: {
            "density_burst": 42,
            "density_sustain": 30,
            "density_decay": 2.5,
            "body_duration_ms_min": 130,
            "body_duration_ms_max": 360,
            "rng_seed": 1012,
            "stereo_width": 0.98,
            "attack_pan_spread": 0.9,
            "sustain_pan_spread": 0.86,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.58,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.5,
            "droplet_enable": 1,
            "droplet_probability": 0.12,
            "droplet_gain": 0.35,
            "memory_mix": 0.48,
            "attack_brightness": 1.72,
            "sustain_darkness": 0.2,
            "quality_profile": 2,
            "active_voice_limit": 24,
            "reverse_probability": 0.1,
            "pitch_mode": 1,
            "shimmer_amount": 0.82,
            "motion_rate": 0.26,
            "motion_depth": 0.26
        },
        macro_values: {
            "density": 0.52,
            "bloom": 0.78,
            "motion": 0.44,
            "texture": 0.56,
            "space": 0.84,
            "gravity": 0.42,
            "memory": 0.54,
            "clarity": 0.68,
            "freeze": 0.34,
            "sparkle": 0.92,
            "warmth": 0.26,
            "mix": 0.64
        },
        metadata: {
            "recommended_min_profile": "WEB_STANDARD",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_STANDARD",
                "active_voice_limit": 24
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.1
                },
                "pitch": {
                    "mode": 1,
                    "shimmer_amount": 0.82
                },
                "motion": {
                    "rate": 0.26,
                    "depth": 0.26,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Warm Nebula",
        preset_slug: "warm-nebula",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "warm",
            "nebula",
            "cinematic",
            "dense",
            "web-standard"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Nebulosa quente e densa, com granulação arredondada e cauda ampla para camadas cinematográficas.",
        params: {
            "density_burst": 34,
            "density_sustain": 42,
            "density_decay": 2,
            "body_duration_ms_min": 180,
            "body_duration_ms_max": 480,
            "rng_seed": 1013,
            "stereo_width": 0.94,
            "sustain_pan_spread": 0.88,
            "wet_drive": 1.18,
            "wet_clip_amount": 0.24,
            "wet_output_trim": 0.92,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.68,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.58,
            "memory_mix": 0.72,
            "memory_pull": 0.64,
            "memory_darkening": 0.54,
            "attack_brightness": 0.68,
            "sustain_darkness": 0.66,
            "quality_profile": 2,
            "active_voice_limit": 24,
            "freeze_amount": 0.36,
            "freeze_enabled": 1,
            "motion_rate": 0.16,
            "motion_depth": 0.2
        },
        macro_values: {
            "density": 0.64,
            "bloom": 0.82,
            "motion": 0.36,
            "texture": 0.52,
            "space": 0.86,
            "gravity": 0.68,
            "memory": 0.78,
            "clarity": 0.26,
            "freeze": 0.56,
            "sparkle": 0.24,
            "warmth": 0.94,
            "mix": 0.7
        },
        metadata: {
            "recommended_min_profile": "WEB_STANDARD",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_STANDARD",
                "active_voice_limit": 24
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.36
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.16,
                    "depth": 0.2,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Slow Motion",
        preset_slug: "slow-motion",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Utility",
        tags: [
            "slow",
            "motion",
            "tape",
            "elastic",
            "mcu-plus"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Textura desacelerada com modulação lenta e envelopes alongados para transformar frases em fita elástica.",
        params: {
            "density_burst": 24,
            "density_sustain": 26,
            "density_decay": 2.5,
            "short_duration_ms_max": 150,
            "body_duration_ms_min": 220,
            "body_duration_ms_max": 520,
            "rng_seed": 1014,
            "stereo_width": 0.82,
            "sustain_pan_spread": 0.72,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.46,
            "sustain_diffusion_stages": 1,
            "sustain_diffusion_feedback": 0.5,
            "memory_mix": 0.56,
            "memory_pull": 0.48,
            "memory_darkening": 0.5,
            "attack_brightness": 0.74,
            "sustain_darkness": 0.62,
            "quality_profile": 1,
            "active_voice_limit": 16,
            "reverse_probability": 0.18,
            "motion_rate": 0.12,
            "motion_depth": 0.78,
            "motion_shape": 1
        },
        macro_values: {
            "density": 0.4,
            "bloom": 0.66,
            "motion": 0.88,
            "texture": 0.5,
            "space": 0.7,
            "gravity": 0.74,
            "memory": 0.62,
            "clarity": 0.38,
            "freeze": 0.4,
            "sparkle": 0.22,
            "warmth": 0.68,
            "mix": 0.56
        },
        metadata: {
            "recommended_min_profile": "MCU_PLUS",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_PLUS",
                "active_voice_limit": 16
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.18
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.12,
                    "depth": 0.78,
                    "shape": 1
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Tape Cloud",
        preset_slug: "tape-cloud",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "tape",
            "lo-fi",
            "cloud",
            "warm",
            "mcu-safe"
        ],
        esp32_safe: true,
        quality_tier: "ESP32_SAFE",
        description: "Nuvem de fita lo-fi, quente e comprimida, com irregularidade suave e brilho aparado.",
        params: {
            "density_burst": 26,
            "density_sustain": 24,
            "density_decay": 3,
            "rng_seed": 1015,
            "stereo_width": 0.78,
            "attack_pan_spread": 0.54,
            "sustain_pan_spread": 0.66,
            "envelope_variation": 0.62,
            "wet_drive": 1.28,
            "wet_clip_amount": 0.32,
            "wet_output_trim": 0.86,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.34,
            "sustain_diffusion_stages": 1,
            "sustain_diffusion_feedback": 0.42,
            "memory_mix": 0.54,
            "memory_darkening": 0.64,
            "tone_variation": 0.7,
            "attack_brightness": 0.62,
            "sustain_darkness": 0.72,
            "attack_rate_jitter": 1,
            "attack_rate_jitter_depth": 0.035,
            "quality_profile": 0,
            "active_voice_limit": 8,
            "motion_rate": 0.2,
            "motion_depth": 0.24
        },
        macro_values: {
            "density": 0.42,
            "bloom": 0.7,
            "motion": 0.42,
            "texture": 0.62,
            "space": 0.66,
            "gravity": 0.58,
            "memory": 0.62,
            "clarity": 0.3,
            "freeze": 0.34,
            "sparkle": 0.18,
            "warmth": 0.88,
            "mix": 0.55
        },
        metadata: {
            "recommended_min_profile": "MCU_SAFE",
            "mcu_compatibility": {
                "compatible": true,
                "profile": "MCU_SAFE",
                "active_voice_limit": 8
            },
            "feature_usage": {
                "freeze": {
                    "enabled": false,
                    "amount": 0.0
                },
                "reverse": {
                    "probability": 0.0
                },
                "pitch": {
                    "mode": 0,
                    "shimmer_amount": 0.0
                },
                "motion": {
                    "rate": 0.2,
                    "depth": 0.24,
                    "shape": 0
                }
            }
        }
    },
    {
        schema_version: 3,
        preset_name: "Cosmic Swell",
        preset_slug: "cosmic-swell",
        engine_version: "post-diffusion-ui",
        created_at: "2026-06-10T00:00:00Z",
        ui_category: "Ambient",
        tags: [
            "cosmic",
            "swell",
            "wide",
            "shimmer",
            "web-ultra"
        ],
        esp32_safe: false,
        quality_tier: "HIGH_QUALITY",
        description: "Swell cósmico amplo com shimmer moderado e movimento orbital para transições e finais de música.",
        params: {
            "burst_immediate_count": 2,
            "density_burst": 30,
            "density_sustain": 50,
            "density_decay": 1.8,
            "body_duration_ms_min": 210,
            "body_duration_ms_max": 520,
            "rng_seed": 1016,
            "stereo_width": 1.0,
            "attack_pan_spread": 0.86,
            "sustain_pan_spread": 0.96,
            "sustain_diffusion_enable": 1,
            "sustain_diffusion_amount": 0.82,
            "sustain_diffusion_stages": 2,
            "sustain_diffusion_feedback": 0.68,
            "droplet_enable": 1,
            "droplet_probability": 0.08,
            "droplet_gain": 0.3,
            "memory_mix": 0.72,
            "memory_pull": 0.58,
            "attack_brightness": 1.12,
            "sustain_darkness": 0.38,
            "quality_profile": 3,
            "active_voice_limit": 32,
            "freeze_amount": 0.52,
            "freeze_enabled": 1,
            "reverse_probability": 0.22,
            "pitch_mode": 1,
            "shimmer_amount": 0.62,
            "motion_rate": 0.38,
            "motion_depth": 0.58,
            "motion_shape": 0
        },
        macro_values: {
            "density": 0.6,
            "bloom": 0.94,
            "motion": 0.7,
            "texture": 0.54,
            "space": 0.96,
            "gravity": 0.5,
            "memory": 0.76,
            "clarity": 0.4,
            "freeze": 0.64,
            "sparkle": 0.74,
            "warmth": 0.48,
            "mix": 0.76
        },
        metadata: {
            "recommended_min_profile": "WEB_ULTRA",
            "mcu_compatibility": {
                "compatible": false,
                "profile": "WEB_ULTRA",
                "active_voice_limit": 32
            },
            "feature_usage": {
                "freeze": {
                    "enabled": true,
                    "amount": 0.52
                },
                "reverse": {
                    "probability": 0.22
                },
                "pitch": {
                    "mode": 1,
                    "shimmer_amount": 0.62
                },
                "motion": {
                    "rate": 0.38,
                    "depth": 0.58,
                    "shape": 0
                }
            }
        }
    }
];

  function createFactoryPresets(baseParams) {
    return CANONICAL_FACTORY_PRESETS.map((preset) => ({
      ...preset,
      params: { ...baseParams, ...preset.params },
      base_params: { ...baseParams, ...preset.params },
      macro_values: { ...preset.macro_values },
      metadata: { ...preset.metadata },
    }));
  }

  global.BubbleCloudFactoryPresets = { createFactoryPresets };
})(window);
