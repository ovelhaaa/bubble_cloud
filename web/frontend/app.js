const BASELINE = {
    noise_floor: 0.001,
    tracking_thresh: 0.01,
    sustain_thresh: 0.05,
    transient_delta: 0.05,
    duck_burst_level: 0.2,
    duck_attack_coef: 0.80,
    duck_release_coef: 0.99,
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
    body_offset_samples: 0,
    body_jitter_samples: 4410,
    master_dry_gain: 0.5,
    master_wet_gain: 0.5
};

const PARAM_HELP = {
  noise_floor: {
    musical: "Sets how quiet the input can get before the effect effectively treats it as silence.",
    technical: "Minimum envelope level below which the tracker is forced toward silence and activity should stop."
  },
  tracking_thresh: {
    musical: "Raises or lowers how easily the effect begins reacting to low-level playing.",
    technical: "Threshold used to distinguish meaningful signal from low-level residual activity after the noise floor."
  },
  sustain_thresh: {
    musical: "Controls how much input energy is needed before the effect feels like it has entered a stable sustained state.",
    technical: "Envelope threshold above which the engine prefers sustain-body behavior instead of sparse decay behavior."
  },
  transient_delta: {
    musical: "Makes the effect more or less eager to explode on note attacks and percussive hits.",
    technical: "Required envelope derivative jump needed to enter transient/burst behavior."
  },
  duck_burst_level: {
    musical: "Sets how far the wet signal ducks during sharp attacks so the dry hit stays clear.",
    technical: "Target wet-gain multiplier used during burst/attack-oriented ducking."
  },
  duck_attack_coef: {
    musical: "Controls how quickly the wet signal ducks when a strong attack happens.",
    technical: "1-pole smoothing coefficient used for the ducking transition toward the burst target."
  },
  duck_release_coef: {
    musical: "Controls how quickly the wet signal recovers after the attack has passed.",
    technical: "1-pole smoothing coefficient used for recovery back toward full wet level."
  },
  burst_duration_ticks: {
    musical: "Determines how long the engine keeps behaving like it is in the burst region after a transient.",
    technical: "Control-rate timer length for transient burst state persistence."
  },
  burst_immediate_count: {
    musical: "Sets how many grains are forced instantly when a strong attack is detected.",
    technical: "Immediate spawn count triggered at transient entry, before normal accumulator-based spawning."
  },
  density_burst: {
    musical: "Controls how busy and explosive the effect feels right after an attack.",
    technical: "Spawn rate in spawns per second while the engine is in burst/attack-oriented behavior."
  },
  density_sustain: {
    musical: "Controls how thick the effect feels while the note is stably ringing.",
    technical: "Spawn rate in spawns per second while the engine is in sustain-body behavior."
  },
  density_decay: {
    musical: "Controls how many bubbles remain as the sound falls away.",
    technical: "Spawn rate in spawns per second while the engine is in sparse decay behavior."
  },
  sustain_read_center_offset_samples: {
    musical: "Pushes the sustain/body material farther back in time, making the texture feel more delayed or more immediate.",
    technical: "Global read-position offset behind the write head used as the main center for body-oriented grain reading."
  },
  micro_duration_ms_min: {
    musical: "Sets the shortest possible length of the smallest attack-oriented grains.",
    technical: "Minimum grain duration in milliseconds for the micro class."
  },
  micro_duration_ms_max: {
    musical: "Sets the longest possible length of the smallest attack-oriented grains.",
    technical: "Maximum grain duration in milliseconds for the micro class."
  },
  micro_offset_samples: {
    musical: "Moves micro grains closer to or farther from the present input, affecting how immediate the attack texture feels.",
    technical: "Base circular-buffer read offset behind the write head for the micro class."
  },
  micro_jitter_samples: {
    musical: "Adds randomness to micro grain placement, making attacks feel either tighter or more scattered.",
    technical: "Random variation range applied to micro-class read offset."
  },
  short_duration_ms_min: {
    musical: "Sets the shortest possible length of the middle-sized grains.",
    technical: "Minimum grain duration in milliseconds for the short class."
  },
  short_duration_ms_max: {
    musical: "Sets the longest possible length of the middle-sized grains.",
    technical: "Maximum grain duration in milliseconds for the short class."
  },
  short_offset_samples: {
    musical: "Moves short grains earlier or later in the recent buffer, affecting how connected or delayed the mid-layer feels.",
    technical: "Base circular-buffer read offset behind the write head for the short class."
  },
  short_jitter_samples: {
    musical: "Adds looseness or spread to the short-grain layer.",
    technical: "Random variation range applied to short-class read offset."
  },
  body_duration_ms_min: {
    musical: "Sets the shortest possible length of the body/sustain grains.",
    technical: "Minimum grain duration in milliseconds for the body class."
  },
  body_duration_ms_max: {
    musical: "Sets the longest possible length of the body/sustain grains.",
    technical: "Maximum grain duration in milliseconds for the body class."
  },
  body_offset_samples: {
    musical: "Fine-tunes where the body grains read from relative to the global sustain position.",
    technical: "Additional base read offset for the body class, applied alongside the sustain read center offset in the current implementation model."
  },
  body_jitter_samples: {
    musical: "Adds spread and variation to the body layer so it feels either tighter or more cloud-like.",
    technical: "Random variation range applied to body-class read offset."
  },
  master_dry_gain: {
    musical: "Sets how much direct unaffected signal stays in front of the bubbles.",
    technical: "Final gain multiplier applied to the dry signal path before output summing."
  },
  master_wet_gain: {
    musical: "Sets how prominent the bubbles are compared with the direct signal.",
    technical: "Final gain multiplier applied to the summed wet buses before output summing."
  }
};

const PARAMETER_GROUPS = [
  {
    name: "Analysis / Detection",
    params: ["noise_floor", "tracking_thresh", "sustain_thresh", "transient_delta"],
  },
  {
    name: "Ducking",
    params: ["duck_burst_level", "duck_attack_coef", "duck_release_coef"],
  },
  {
    name: "Burst",
    params: ["burst_duration_ticks", "burst_immediate_count"],
  },
  {
    name: "Density",
    params: ["density_burst", "density_sustain", "density_decay"],
  },
  {
    name: "Sustain Read Position",
    params: ["sustain_read_center_offset_samples"],
  },
  {
    name: "Micro Class",
    params: [
      "micro_duration_ms_min",
      "micro_duration_ms_max",
      "micro_offset_samples",
      "micro_jitter_samples",
    ],
  },
  {
    name: "Short Class",
    params: [
      "short_duration_ms_min",
      "short_duration_ms_max",
      "short_offset_samples",
      "short_jitter_samples",
    ],
  },
  {
    name: "Body Class",
    params: ["body_duration_ms_min", "body_duration_ms_max", "body_offset_samples", "body_jitter_samples"],
  },
  {
    name: "Output Mix",
    params: ["master_dry_gain", "master_wet_gain"],
  },
];

const FIELD_CONFIG = {
  noise_floor: { min: 0, max: 0.1, step: 0.001 },
  tracking_thresh: { min: 0, max: 0.5, step: 0.005 },
  sustain_thresh: { min: 0, max: 0.5, step: 0.005 },
  transient_delta: { min: 0, max: 0.5, step: 0.005 },
  duck_burst_level: { min: 0, max: 1, step: 0.01 },
  duck_attack_coef: { min: 0.5, max: 0.999, step: 0.001 },
  duck_release_coef: { min: 0.5, max: 0.999, step: 0.001 },
  burst_duration_ticks: { min: 0, max: 100, step: 1 },
  burst_immediate_count: { min: 0, max: 20, step: 1 },
  density_burst: { min: 0, max: 200, step: 1 },
  density_sustain: { min: 0, max: 100, step: 1 },
  density_decay: { min: 0, max: 50, step: 1 },
  sustain_read_center_offset_samples: { min: 0, max: 88200, step: 10 },
  micro_duration_ms_min: { min: 1, max: 50, step: 0.5 },
  micro_duration_ms_max: { min: 1, max: 100, step: 0.5 },
  micro_offset_samples: { min: 0, max: 88200, step: 10 },
  micro_jitter_samples: { min: 0, max: 44100, step: 10 },
  short_duration_ms_min: { min: 10, max: 100, step: 1 },
  short_duration_ms_max: { min: 10, max: 200, step: 1 },
  short_offset_samples: { min: 0, max: 88200, step: 10 },
  short_jitter_samples: { min: 0, max: 44100, step: 10 },
  body_duration_ms_min: { min: 50, max: 500, step: 1 },
  body_duration_ms_max: { min: 50, max: 1000, step: 1 },
  body_offset_samples: { min: 0, max: 88200, step: 10 },
  body_jitter_samples: { min: 0, max: 88200, step: 10 },
  master_dry_gain: { min: 0, max: 2, step: 0.01 },
  master_wet_gain: { min: 0, max: 2, step: 0.01 },
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
    density_burst: { min: 0.0, max: 100.0, step: 1.0 },
    density_sustain: { min: 0.0, max: 40.0, step: 1.0 },
    density_decay: { min: 0.0, max: 20.0, step: 1.0 },
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
        paramHelp: PARAM_HELP,
        openAccordion: null,
        openHelp: {},

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

        sanitizeDurations() {
            if (this.params.micro_duration_ms_min > this.params.micro_duration_ms_max) {
                this.params.micro_duration_ms_max = this.params.micro_duration_ms_min;
            }
            if (this.params.short_duration_ms_min > this.params.short_duration_ms_max) {
                this.params.short_duration_ms_max = this.params.short_duration_ms_min;
            }
            if (this.params.body_duration_ms_min > this.params.body_duration_ms_max) {
                this.params.body_duration_ms_max = this.params.body_duration_ms_min;
            }
        },

        // Audio Worklet Integration
        audioInitialized: false,
        isPlaying: false,
        audioContext: null,
        oscillator: null,
        workletNode: null,
        audioStateText: "Not Started",
        metrics: {
            envelope: 0,
            state: 0,
            voices: 0
        },
        pollInterval: null,

        async initAudio() {
            try {
                this.audioStateText = "Initializing AudioContext...";
                this.audioContext = new (window.AudioContext || window.webkitAudioContext)();

                this.audioStateText = "Loading AudioWorklet...";
                // Make sure bubble_cloud_wasm.js is loaded internally by the worklet
                await this.audioContext.audioWorklet.addModule('worklet.js');

                this.workletNode = new AudioWorkletNode(this.audioContext, 'sound-bubbles-worklet');

                // Set audioInitialized immediately so UI updates
                this.audioInitialized = true;

                // Wait for the worklet to be ready
                this.workletNode.port.onmessage = (e) => {
                    if (e.data.type === 'ready') {
                        this.audioStateText = "Ready";
                        this.pushAllParamsToAudio();

                        // Start polling metrics
                        this.pollInterval = setInterval(() => {
                            if (this.audioInitialized && this.workletNode) {
                                this.workletNode.port.postMessage({type: 'poll'});
                            }
                        }, 100);
                    } else if (e.data.type === 'state') {
                        this.metrics.envelope = e.data.envelope;
                        this.metrics.state = e.data.state;
                        this.metrics.voices = e.data.voices;
                    }
                };

                this.workletNode.connect(this.audioContext.destination);

            } catch (err) {
                console.error("Audio initialization failed:", err);
                this.audioStateText = "Error initializing audio. Check console.";
            }
        },

        togglePlayback() {
            if (!this.audioInitialized) return;

            if (this.isPlaying) {
                // Stop
                if (this.oscillator) {
                    this.oscillator.stop();
                    this.oscillator.disconnect();
                    this.oscillator = null;
                }
                this.isPlaying = false;
                this.audioStateText = "Stopped";
            } else {
                // Play a simple test signal (e.g. short pulses to simulate plucks)
                this.oscillator = this.audioContext.createOscillator();
                this.oscillator.type = 'sawtooth';
                this.oscillator.frequency.value = 220;

                // Add an envelope to make it plucky
                const gainNode = this.audioContext.createGain();
                gainNode.gain.value = 0;

                this.oscillator.connect(gainNode);
                gainNode.connect(this.workletNode);

                this.oscillator.start();

                // Pluck loop
                const pluck = () => {
                    if (!this.isPlaying) return;
                    const now = this.audioContext.currentTime;
                    gainNode.gain.setValueAtTime(0, now);
                    gainNode.gain.linearRampToValueAtTime(0.8, now + 0.01);
                    gainNode.gain.exponentialRampToValueAtTime(0.001, now + 0.5);
                    setTimeout(pluck, 1000); // 1 pluck per second
                };

                this.isPlaying = true;
                this.audioStateText = "Playing Plucks";
                pluck();
            }
        },

        pushParamToAudio(key, val) {
            if (!this.audioInitialized || !this.workletNode) return;

            const paramMap = {
                "noise_floor": 0, "tracking_thresh": 1, "sustain_thresh": 2, "transient_delta": 3,
                "duck_burst_level": 4, "duck_attack_coef": 5, "duck_release_coef": 6,
                "burst_duration_ticks": 7, "burst_immediate_count": 8,
                "density_burst": 9, "density_sustain": 10, "density_decay": 11,
                "sustain_read_center_offset_samples": 12,
                "micro_duration_ms_min": 13, "micro_duration_ms_max": 14, "micro_offset_samples": 15, "micro_jitter_samples": 16,
                "short_duration_ms_min": 17, "short_duration_ms_max": 18, "short_offset_samples": 19, "short_jitter_samples": 20,
                "body_duration_ms_min": 21, "body_duration_ms_max": 22, "body_offset_samples": 23, "body_jitter_samples": 24,
                "master_dry_gain": 25, "master_wet_gain": 26
            };

            if (key in paramMap) {
                this.workletNode.port.postMessage({
                    type: 'param',
                    id: paramMap[key],
                    value: val
                });
            }
        },

        pushAllParamsToAudio() {
            for (const key in this.params) {
                this.pushParamToAudio(key, this.params[key]);
            }
        },

        getEngineStateName(stateCode) {
            const states = ["Silence", "Transient Burst", "Attack Ongoing", "Sustain Body", "Sparse Decay"];
            return states[stateCode] || "Unknown";
        },

        saveDraft() {
            // Keep JSON absolutely flat, ignoring UI grouping logic
            this.sanitizeDurations();
            localStorage.setItem('sb_draft', JSON.stringify(this.params, null, 2));
            this.isDraft = true;

            // Push updates to audio engine
            if (this.audioInitialized) {
                this.pushAllParamsToAudio();
            }
        },

        resetToBaseline() {
            if (confirm("Reset local draft to baseline defaults? You will lose any unsaved edits.")) {
                this.params = { ...BASELINE };
                this.isDraft = false;
                localStorage.removeItem('sb_draft');

                if (this.audioInitialized) {
                    this.pushAllParamsToAudio();
                }
            }
        },

        toggleAccordion(groupName) {
            this.openAccordion = this.openAccordion === groupName ? null : groupName;
        },

        toggleHelp(paramKey) {
            this.openHelp[paramKey] = !this.openHelp[paramKey];
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
            this.sanitizeDurations();
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
            this.sanitizeDurations();
            const jsonStr = JSON.stringify(this.params, null, 2);
            const blob = new Blob([jsonStr], { type: "application/json" });
            const url = URL.createObjectURL(blob);
            const a = document.createElement("a");
            a.href = url;
            a.download = "current.json";
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }
    }));
});
