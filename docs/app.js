const BASELINE = {
    noise_floor: 0.001,
    tracking_thresh: 0.01,
    sustain_thresh: 0.1,
    transient_delta: 0.05,
    duck_burst_level: 0.2,
    duck_attack_coef: 0.99,
    duck_release_coef: 0.999,
    burst_duration_ticks: 10,
    burst_immediate_count: 3,
    density_burst: 50.0,
    density_sustain: 15.0,
    density_decay: 5.0,
    sustain_read_center_offset_samples: 22050,
    micro_duration_ms_min: 5.0,
    micro_duration_ms_max: 15.0,
    micro_offset_samples: 441,
    micro_jitter_samples: 100,
    short_duration_ms_min: 20.0,
    short_duration_ms_max: 50.0,
    short_offset_samples: 4410,
    short_jitter_samples: 500,
    body_duration_ms_min: 80.0,
    body_duration_ms_max: 200.0,
    body_offset_samples: 22050,
    body_jitter_samples: 4410,
    master_dry_gain: 0.5,
    master_wet_gain: 0.5
};

// Logical grouping for UI accordions
const PARAMETER_GROUPS = {
    "Analysis / Detection": [
        "noise_floor", "tracking_thresh", "sustain_thresh", "transient_delta"
    ],
    "Ducking": [
        "duck_burst_level", "duck_attack_coef", "duck_release_coef"
    ],
    "Burst": [
        "burst_duration_ticks", "burst_immediate_count"
    ],
    "Density": [
        "density_burst", "density_sustain", "density_decay"
    ],
    "Sustain Read Position": [
        "sustain_read_center_offset_samples"
    ],
    "Micro Class": [
        "micro_duration_ms_min", "micro_duration_ms_max", "micro_offset_samples", "micro_jitter_samples"
    ],
    "Short Class": [
        "short_duration_ms_min", "short_duration_ms_max", "short_offset_samples", "short_jitter_samples"
    ],
    "Body Class": [
        "body_duration_ms_min", "body_duration_ms_max", "body_offset_samples", "body_jitter_samples"
    ],
    "Output Mix": [
        "master_dry_gain", "master_wet_gain"
    ]
};

// Provides min, max, and sensible step values for the thick sliders
const PARAM_CONFIGS = {
    // Analysis
    noise_floor: { min: 0.0, max: 0.1, step: 0.001 },
    tracking_thresh: { min: 0.0, max: 0.5, step: 0.01 },
    sustain_thresh: { min: 0.0, max: 0.5, step: 0.01 },
    transient_delta: { min: 0.0, max: 0.5, step: 0.01 },
    // Ducking
    duck_burst_level: { min: 0.0, max: 1.0, step: 0.01 },
    duck_attack_coef: { min: 0.5, max: 0.999, step: 0.001 },
    duck_release_coef: { min: 0.5, max: 0.999, step: 0.001 },
    // Burst
    burst_duration_ticks: { min: 0, max: 100, step: 1 },
    burst_immediate_count: { min: 0, max: 20, step: 1 },
    // Density (Spawns per second)
    density_burst: { min: 0.0, max: 200.0, step: 1.0 },
    density_sustain: { min: 0.0, max: 100.0, step: 1.0 },
    density_decay: { min: 0.0, max: 50.0, step: 1.0 },
    // Global Read Offset
    sustain_read_center_offset_samples: { min: 0, max: 88200, step: 100 },
    // Micro
    micro_duration_ms_min: { min: 1.0, max: 50.0, step: 0.5 },
    micro_duration_ms_max: { min: 1.0, max: 100.0, step: 0.5 },
    micro_offset_samples: { min: 0, max: 88200, step: 10 },
    micro_jitter_samples: { min: 0, max: 44100, step: 10 },
    // Short
    short_duration_ms_min: { min: 10.0, max: 100.0, step: 1.0 },
    short_duration_ms_max: { min: 10.0, max: 200.0, step: 1.0 },
    short_offset_samples: { min: 0, max: 88200, step: 100 },
    short_jitter_samples: { min: 0, max: 44100, step: 100 },
    // Body
    body_duration_ms_min: { min: 50.0, max: 500.0, step: 5.0 },
    body_duration_ms_max: { min: 50.0, max: 1000.0, step: 5.0 },
    body_offset_samples: { min: 0, max: 88200, step: 100 },
    body_jitter_samples: { min: 0, max: 88200, step: 100 },
    // Mix
    master_dry_gain: { min: 0.0, max: 2.0, step: 0.01 },
    master_wet_gain: { min: 0.0, max: 2.0, step: 0.01 },
};

document.addEventListener('alpine:init', () => {
    Alpine.data('editorApp', () => ({
        params: {},
        isDraft: false,
        copied: false,
        parameterGroups: PARAMETER_GROUPS,
        openAccordion: null,

        init() {
            this.loadState();
        },

        loadState() {
            const saved = localStorage.getItem('sb_draft');
            if (saved) {
                try {
                    this.params = { ...BASELINE, ...JSON.parse(saved) };
                    this.isDraft = true;
                    return;
                } catch (e) {
                    console.error("Failed to parse localStorage draft", e);
                }
            }
            // Fallback
            this.params = { ...BASELINE };
            this.isDraft = false;
        },

        saveDraft() {
            // Keep JSON absolutely flat, ignoring UI grouping logic
            localStorage.setItem('sb_draft', JSON.stringify(this.params, null, 2));
            this.isDraft = true;
        },

        resetToBaseline() {
            if (confirm("Reset local draft to baseline defaults? You will lose any unsaved edits.")) {
                this.params = { ...BASELINE };
                this.isDraft = false;
                localStorage.removeItem('sb_draft');
            }
        },

        toggleAccordion(groupName) {
            this.openAccordion = this.openAccordion === groupName ? null : groupName;
        },

        formatLabel(key) {
            return key.replace(/_/g, ' ')
                      .replace(/\b\w/g, c => c.toUpperCase());
        },

        getParamConfig(key) {
            // Fallback just in case a parameter isn't explicitly configured
            return PARAM_CONFIGS[key] || { min: 0, max: 100, step: 1 };
        },

        copyJson() {
            const jsonStr = JSON.stringify(this.params, null, 2);
            navigator.clipboard.writeText(jsonStr).then(() => {
                this.copied = true;
                setTimeout(() => this.copied = false, 2000);
            }).catch(err => {
                console.error("Could not copy text: ", err);
                alert("Failed to copy JSON. Please try downloading it instead.");
            });
        },

        downloadJson() {
            const jsonStr = JSON.stringify(this.params, null, 2);
            const blob = new Blob([jsonStr], { type: "application/json" });
            const url = URL.createObjectURL(blob);
            const a = document.createElement("a");
            a.href = url;
            a.download = "current.json";
            a.style.display = "none";
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }
    }));
});