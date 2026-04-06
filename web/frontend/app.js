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
    attack_region_min_offset_samples: 441,
    attack_region_max_offset_samples: 3528,
    body_region_min_offset_samples: 3528,
    body_region_max_offset_samples: 11025,
    memory_region_min_offset_samples: 11025,
    memory_region_max_offset_samples: 39690,
    micro_duration_ms_min: 5.0,
    micro_duration_ms_max: 15.0,
    short_duration_ms_min: 20.0,
    short_duration_ms_max: 50.0,
    body_duration_ms_min: 80.0,
    body_duration_ms_max: 200.0,
    rng_seed: 1,
    mix_dry_gain: 0.5,
    mix_wet_gain: 0.5
};

const PARAM_HELP = {
  noise_floor: { musical: "Sets how quiet the input can get before the effect effectively treats it as silence.", technical: "Minimum envelope level below which the tracker is forced toward silence and activity should stop." },
  tracking_thresh: { musical: "Raises or lowers how easily the effect begins reacting to low-level playing.", technical: "Threshold used to distinguish meaningful signal from low-level residual activity after the noise floor." },
  sustain_thresh: { musical: "Controls how much input energy is needed before the effect feels like it has entered a stable sustained state.", technical: "Envelope threshold above which the engine prefers sustain-body behavior instead of sparse decay behavior." },
  transient_delta: { musical: "Makes the effect more or less eager to explode on note attacks and percussive hits.", technical: "Required envelope derivative jump needed to enter transient/burst behavior." },
  duck_burst_level: { musical: "Sets how far the wet signal ducks during sharp attacks so the dry hit stays clear.", technical: "Target wet-gain multiplier used during burst/attack-oriented ducking." },
  duck_attack_coef: { musical: "Controls how quickly the wet signal ducks when a strong attack happens.", technical: "1-pole smoothing coefficient used for the ducking transition toward the burst target." },
  duck_release_coef: { musical: "Controls how quickly the wet signal recovers after the attack has passed.", technical: "1-pole smoothing coefficient used for recovery back toward full wet level." },
  burst_duration_ticks: { musical: "Determines how long the engine keeps behaving like it is in the burst region after a transient.", technical: "Control-rate timer length for transient burst state persistence." },
  burst_immediate_count: { musical: "Sets how many grains are forced instantly when a strong attack is detected.", technical: "Immediate spawn count triggered at transient entry, before normal accumulator-based spawning." },
  density_burst: { musical: "Controls how busy and explosive the effect feels right after an attack.", technical: "Spawn rate in spawns per second while the engine is in burst/attack-oriented behavior." },
  density_sustain: { musical: "Controls how thick the effect feels while the note is stably ringing.", technical: "Spawn rate in spawns per second while the engine is in sustain-body behavior." },
  density_decay: { musical: "Controls how many bubbles remain as the sound falls away.", technical: "Spawn rate in spawns per second while the engine is in sparse decay behavior." },
  attack_region_min_offset_samples: { musical: "Sets the nearest read point for attack grains, controlling how immediate the edge texture feels.", technical: "Minimum circular-buffer read offset behind the write head for the attack semantic region." },
  attack_region_max_offset_samples: { musical: "Sets the farthest read point for attack grains, widening or tightening attack-region spread.", technical: "Maximum circular-buffer read offset behind the write head for the attack semantic region." },
  body_region_min_offset_samples: { musical: "Sets the nearest read point for body grains, controlling how quickly sustain texture appears.", technical: "Minimum circular-buffer read offset behind the write head for the body semantic region." },
  body_region_max_offset_samples: { musical: "Sets the farthest read point for body grains, changing sustain depth and timing spread.", technical: "Maximum circular-buffer read offset behind the write head for the body semantic region." },
  memory_region_min_offset_samples: { musical: "Sets the nearest read point for long-memory texture, from recent tail to more delayed echoes.", technical: "Minimum circular-buffer read offset behind the write head for the memory semantic region." },
  memory_region_max_offset_samples: { musical: "Sets the farthest read point for memory texture, controlling how far back the cloud can reach.", technical: "Maximum circular-buffer read offset behind the write head for the memory semantic region." },
  micro_duration_ms_min: { musical: "Sets the shortest possible length of the smallest attack-oriented grains.", technical: "Minimum grain duration in milliseconds for the micro class." },
  micro_duration_ms_max: { musical: "Sets the longest possible length of the smallest attack-oriented grains.", technical: "Maximum grain duration in milliseconds for the micro class." },
  short_duration_ms_min: { musical: "Sets the shortest possible length of the middle-sized grains.", technical: "Minimum grain duration in milliseconds for the short class." },
  short_duration_ms_max: { musical: "Sets the longest possible length of the middle-sized grains.", technical: "Maximum grain duration in milliseconds for the short class." },
  body_duration_ms_min: { musical: "Sets the shortest possible length of the body/sustain grains.", technical: "Minimum grain duration in milliseconds for the body class." },
  body_duration_ms_max: { musical: "Sets the longest possible length of the body/sustain grains.", technical: "Maximum grain duration in milliseconds for the body class." },
  rng_seed: { musical: "Locks the random pattern so presets feel repeatable across renders and sessions.", technical: "Initial PRNG seed used by deterministic voice spawn/read-position sampling." },
  mix_dry_gain: { musical: "Sets how much direct unaffected signal stays in front of the bubbles.", technical: "Final gain multiplier applied to the dry signal path before output summing." },
  mix_wet_gain: { musical: "Sets how prominent the bubbles are compared with the direct signal.", technical: "Final gain multiplier applied to the summed wet buses before output summing." }
};

const PARAMETER_GROUPS = [
  { name: "Analysis / Detection", params: ["noise_floor", "tracking_thresh", "sustain_thresh", "transient_delta"] },
  { name: "Ducking", params: ["duck_burst_level", "duck_attack_coef", "duck_release_coef"] },
  { name: "Burst", params: ["burst_duration_ticks", "burst_immediate_count"] },
  { name: "Density", params: ["density_burst", "density_sustain", "density_decay"] },
  { name: "Attack Region (Offsets)", params: ["attack_region_min_offset_samples", "attack_region_max_offset_samples"] },
  { name: "Body Region (Offsets)", params: ["body_region_min_offset_samples", "body_region_max_offset_samples"] },
  { name: "Memory Region (Offsets)", params: ["memory_region_min_offset_samples", "memory_region_max_offset_samples"] },
  { name: "Attack Region (Durations)", params: ["micro_duration_ms_min", "micro_duration_ms_max"] },
  { name: "Body Region (Durations)", params: ["short_duration_ms_min", "short_duration_ms_max"] },
  { name: "Memory Region (Durations)", params: ["body_duration_ms_min", "body_duration_ms_max"] },
  { name: "Randomness", params: ["rng_seed"] },
  { name: "Output Mix", params: ["mix_dry_gain", "mix_wet_gain"] },
];

const PARAM_LABELS = {
    attack_region_min_offset_samples: "Attack Region Min Offset (samples)",
    attack_region_max_offset_samples: "Attack Region Max Offset (samples)",
    body_region_min_offset_samples: "Body Region Min Offset (samples)",
    body_region_max_offset_samples: "Body Region Max Offset (samples)",
    memory_region_min_offset_samples: "Memory Region Min Offset (samples)",
    memory_region_max_offset_samples: "Memory Region Max Offset (samples)",
    micro_duration_ms_min: "Attack Region Min Duration (ms)",
    micro_duration_ms_max: "Attack Region Max Duration (ms)",
    short_duration_ms_min: "Body Region Min Duration (ms)",
    short_duration_ms_max: "Body Region Max Duration (ms)",
    body_duration_ms_min: "Memory Region Min Duration (ms)",
    body_duration_ms_max: "Memory Region Max Duration (ms)",
};

const WASM_PARAM_ID_MAP = {
    noise_floor: 0,
    tracking_thresh: 1,
    sustain_thresh: 2,
    transient_delta: 3,
    duck_burst_level: 4,
    duck_attack_coef: 5,
    duck_release_coef: 6,
    burst_duration_ticks: 7,
    burst_immediate_count: 8,
    density_burst: 9,
    density_sustain: 10,
    density_decay: 11,
    attack_region_min_offset_samples: 12,
    attack_region_max_offset_samples: 13,
    body_region_min_offset_samples: 14,
    body_region_max_offset_samples: 15,
    memory_region_min_offset_samples: 16,
    memory_region_max_offset_samples: 17,
    micro_duration_ms_min: 18,
    micro_duration_ms_max: 19,
    short_duration_ms_min: 20,
    short_duration_ms_max: 21,
    body_duration_ms_min: 22,
    body_duration_ms_max: 23,
    rng_seed: 24,
    mix_dry_gain: 25,
    mix_wet_gain: 26
};

const PARITY_NOTE = "WASM preview is inspection/demo only; sonic behavior must stay in the shared core per docs/SONIC_PARITY_CONTRACT.md.";

const PARAM_CONFIGS = {
    noise_floor: { min: 0.0, max: 0.1, step: 0.001 },
    tracking_thresh: { min: 0.0, max: 0.5, step: 0.01 },
    sustain_thresh: { min: 0.0, max: 0.5, step: 0.01 },
    transient_delta: { min: 0.0, max: 0.5, step: 0.01 },
    duck_burst_level: { min: 0.0, max: 1.0, step: 0.01 },
    duck_attack_coef: { min: 0.5, max: 0.999, step: 0.001 },
    duck_release_coef: { min: 0.5, max: 0.999, step: 0.001 },
    burst_duration_ticks: { min: 0, max: 100, step: 1 },
    burst_immediate_count: { min: 0, max: 20, step: 1 },
    density_burst: { min: 0.0, max: 100.0, step: 1.0 },
    density_sustain: { min: 0.0, max: 40.0, step: 1.0 },
    density_decay: { min: 0.0, max: 20.0, step: 1.0 },
    attack_region_min_offset_samples: { min: 0, max: 88200, step: 1 },
    attack_region_max_offset_samples: { min: 0, max: 88200, step: 1 },
    body_region_min_offset_samples: { min: 0, max: 88200, step: 1 },
    body_region_max_offset_samples: { min: 0, max: 88200, step: 1 },
    memory_region_min_offset_samples: { min: 0, max: 88200, step: 1 },
    memory_region_max_offset_samples: { min: 0, max: 88200, step: 1 },
    micro_duration_ms_min: { min: 1.0, max: 50.0, step: 0.5 },
    micro_duration_ms_max: { min: 1.0, max: 100.0, step: 0.5 },
    short_duration_ms_min: { min: 10.0, max: 100.0, step: 1.0 },
    short_duration_ms_max: { min: 10.0, max: 200.0, step: 1.0 },
    body_duration_ms_min: { min: 50.0, max: 500.0, step: 5.0 },
    body_duration_ms_max: { min: 50.0, max: 1000.0, step: 5.0 },
    rng_seed: { min: 1, max: 2147483647, step: 1 },
    mix_dry_gain: { min: 0.0, max: 2.0, step: 0.01 },
    mix_wet_gain: { min: 0.0, max: 2.0, step: 0.01 },
};

document.addEventListener('alpine:init', () => {
    Alpine.data('editorApp', () => ({
        // State
        params: {},
        isDraft: false,
        hasUnexportedChanges: false,
        parameterGroups: PARAMETER_GROUPS,
        paramHelp: PARAM_HELP,
        parityNote: PARITY_NOTE,
        openAccordion: "Analysis / Detection",
        openHelp: {},
        showResetModal: false,
        toasts: [],
        toastId: 0,

        // Audio Transport State
        audioInitialized: false,
        isPlaying: false,
        previewMode: 'file', // 'file' or 'synth'

        // Custom File State
        dragOver: false,
        audioFile: null,
        audioBuffer: null,
        isDecoding: false,

        // Playback State
        audioContext: null,
        workletNode: null,
        sourceNode: null, // oscillator or file buffer
        gainNode: null,   // for synth envelope
        audioStatusMsg: "Não iniciado",
        audioError: false,

        // Progress tracking
        currentTime: 0,
        duration: 0,
        seekTime: 0,
        isDraggingSeek: false,
        startTimeContext: 0,
        pauseTimeContext: 0,
        animationFrameId: null,

        // Metrics
        metrics: { envelope: 0, state: 0, voices: 0 },
        pollInterval: null,

        migrateLegacyPreset(rawParams) {
            const migrated = { ...rawParams };

            const attackMin = migrated.attack_region_min_offset_samples ?? migrated.micro_offset_samples;
            const attackMax = migrated.attack_region_max_offset_samples ??
                ((migrated.micro_offset_samples ?? 441) + (migrated.micro_jitter_samples ?? 3087));
            const bodyMin = migrated.body_region_min_offset_samples ?? migrated.short_offset_samples;
            const bodyMax = migrated.body_region_max_offset_samples ??
                ((migrated.short_offset_samples ?? 3528) + (migrated.short_jitter_samples ?? 7497));
            const memoryMin = migrated.memory_region_min_offset_samples ?? migrated.body_offset_samples;
            const memoryMax = migrated.memory_region_max_offset_samples ??
                ((migrated.body_offset_samples ?? 11025) + (migrated.body_jitter_samples ?? 28665));

            return {
                ...migrated,
                attack_region_min_offset_samples: attackMin ?? BASELINE.attack_region_min_offset_samples,
                attack_region_max_offset_samples: attackMax ?? BASELINE.attack_region_max_offset_samples,
                body_region_min_offset_samples: bodyMin ?? BASELINE.body_region_min_offset_samples,
                body_region_max_offset_samples: bodyMax ?? BASELINE.body_region_max_offset_samples,
                memory_region_min_offset_samples: memoryMin ?? BASELINE.memory_region_min_offset_samples,
                memory_region_max_offset_samples: memoryMax ?? BASELINE.memory_region_max_offset_samples,
                mix_dry_gain: migrated.mix_dry_gain ?? migrated.master_dry_gain ?? BASELINE.mix_dry_gain,
                mix_wet_gain: migrated.mix_wet_gain ?? migrated.master_wet_gain ?? BASELINE.mix_wet_gain,
                rng_seed: migrated.rng_seed ?? BASELINE.rng_seed
            };
        },

        init() {
            this.loadState();
        },

        loadState() {
            const saved = localStorage.getItem('sb_draft');
            if (saved) {
                try {
                    this.params = { ...BASELINE, ...this.migrateLegacyPreset(JSON.parse(saved)) };
                    this.isDraft = true;
                    this.hasUnexportedChanges = true;
                    return;
                } catch (e) {
                    console.error("Failed to parse localStorage draft", e);
                }
            }
            this.params = { ...BASELINE };
            this.isDraft = false;
            this.hasUnexportedChanges = false;
        },

        // --- Toasts UI ---
        showToast(message, type = 'success') {
            const id = this.toastId++;
            this.toasts.push({ id, message, type });
            setTimeout(() => {
                this.toasts = this.toasts.filter(t => t.id !== id);
            }, 3000);
        },

        // --- Core UI Logic ---
        saveDraft() {
            this.sanitizeDurations();
            localStorage.setItem('sb_draft', JSON.stringify(this.params, null, 2));
            this.isDraft = true;
            this.hasUnexportedChanges = true;

            if (this.audioInitialized) {
                this.pushAllParamsToAudio();
            }
        },

        confirmReset() {
            this.params = { ...BASELINE };
            this.isDraft = false;
            this.hasUnexportedChanges = false;
            localStorage.removeItem('sb_draft');

            if (this.audioInitialized) {
                this.pushAllParamsToAudio();
            }
            this.showResetModal = false;
            this.showToast('Reset to baseline settings.', 'info');
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

        toggleAccordion(groupName) {
            this.openAccordion = this.openAccordion === groupName ? null : groupName;
        },

        toggleHelp(paramKey) {
            this.openHelp[paramKey] = !this.openHelp[paramKey];
        },

        formatLabel(key) {
            if (key in PARAM_LABELS) return PARAM_LABELS[key];
            return key.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase());
        },

        getParamConfig(key) {
            return PARAM_CONFIGS[key] || { min: 0, max: 100, step: 1 };
        },

        // --- Data Export ---
        copyJson() {
            this.sanitizeDurations();
            const jsonStr = JSON.stringify(this.params, null, 2);
            navigator.clipboard.writeText(jsonStr).then(() => {
                this.showToast('JSON Copied to Clipboard!');
                this.hasUnexportedChanges = false;
            }).catch(err => {
                console.error("Could not copy: ", err);
                this.showToast('Failed to copy JSON.', 'error');
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

            this.showToast('JSON Downloaded successfully!');
            this.hasUnexportedChanges = false;
        },

        // --- Audio File Handling ---
        handleFileDrop(event) {
            this.dragOver = false;
            if (event.dataTransfer.files && event.dataTransfer.files.length > 0) {
                this.processSelectedFile(event.dataTransfer.files[0]);
            }
        },

        handleFileSelect(event) {
            if (event.target.files && event.target.files.length > 0) {
                this.processSelectedFile(event.target.files[0]);
            }
        },

        async processSelectedFile(file) {
            console.log("[File] Processing selected file:", file.name, "Type:", file.type);
            if (!file.type.startsWith('audio/')) {
                console.error("[File] Invalid format.");
                this.showToast('Formato inválido. Use .wav, .mp3, etc.', 'error');
                return;
            }

            this.stopPlay();
            this.audioFile = file;
            this.audioBuffer = null;
            this.currentTime = 0;
            this.seekTime = 0;
            this.duration = 0;
            this.previewMode = 'file';

            if (this.audioInitialized) {
                this.decodeAudioFile();
            } else {
                console.log("[File] Waiting for audio engine to decode:", file.name);
                this.audioStatusMsg = "Pronto para decodificar (Ative o Engine)";
            }
        },

        clearAudioFile() {
            console.log("[File] Clearing audio file.");
            this.stopPlay();
            this.audioFile = null;
            this.audioBuffer = null;
            this.currentTime = 0;
            this.seekTime = 0;
            this.duration = 0;
            document.getElementById('audio-upload').value = '';

            if (this.previewMode === 'file') {
                this.previewMode = 'synth';
            }
        },

        async decodeAudioFile() {
            if (!this.audioFile || !this.audioContext) return;

            console.log("[File] Decoding file:", this.audioFile.name);
            this.isDecoding = true;
            this.audioStatusMsg = "Decodificando áudio...";
            this.audioError = false;

            try {
                const arrayBuffer = await this.audioFile.arrayBuffer();
                this.audioBuffer = await this.audioContext.decodeAudioData(arrayBuffer);
                this.duration = this.audioBuffer.duration;
                console.log("[File] Decode success. Duration:", this.duration);
                this.audioStatusMsg = "Pronto para tocar";
                this.isDecoding = false;
            } catch (err) {
                console.error("[File] Audio decode error:", err);
                this.isDecoding = false;
                this.audioError = true;
                this.audioStatusMsg = "Falha ao decodificar áudio";
                this.showToast("Erro ao decodificar. Tente outro arquivo.", "error");
                this.audioBuffer = null;
            }
        },

        // --- Web Audio Initialization ---
        async initAudio() {
            try {
                console.log("[Audio Engine] Starting initAudio()...");
                this.audioStatusMsg = "Inicializando AudioContext...";

                // Only create if it doesn't exist
                if (!this.audioContext) {
                    console.log("[Audio Engine] Creating new AudioContext...");
                    this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
                }

                console.log("[Audio Engine] Context state before resume:", this.audioContext.state);
                if (this.audioContext.state === 'suspended') {
                    await this.audioContext.resume();
                    console.log("[Audio Engine] Context resumed. New state:", this.audioContext.state);
                }

                this.audioStatusMsg = "Carregando WASM Processor...";
                console.log("[Audio Engine] Loading worklet module...");

                try {
                    await this.audioContext.audioWorklet.addModule('worklet.js', { type: 'module' });
                    console.log("[Audio Engine] Worklet module loaded.");
                } catch (moduleErr) {
                    console.error("[Audio Engine] Error loading worklet module:", moduleErr);
                    throw moduleErr;
                }

                console.log("[Audio Engine] Creating AudioWorkletNode...");
                this.workletNode = new AudioWorkletNode(this.audioContext, 'sound-bubbles-worklet', {
                    outputChannelCount: [2]
                });

                this.workletNode.port.onmessage = (e) => {
                    if (e.data.type === 'ready') {
                        console.log("[Audio Engine] DSP Engine is ready.");
                        this.audioInitialized = true;
                        this.audioStatusMsg = "Pronto";
                        this.pushAllParamsToAudio();
                        this.startMetricsPolling();

                        // If file was queued, decode it now
                        if (this.audioFile && !this.audioBuffer) {
                            this.decodeAudioFile();
                        }
                    } else if (e.data.type === 'state') {
                        this.metrics.envelope = e.data.envelope;
                        this.metrics.state = e.data.state;
                        this.metrics.voices = e.data.voices;
                    } else if (e.data.type === 'error' || e.data.type === 'init-failed' || e.data.type === 'wasm-error') {
                        console.error("[Audio Engine] Worklet error:", e.data.message);
                        this.audioError = true;
                        this.audioStatusMsg = "Erro no Engine: " + (e.data.message || "Falha desconhecida");
                        this.showToast("Falha no WASM: " + (e.data.message || "Erro desconhecido"), "error");
                    }
                };

                console.log("[Audio Engine] Connecting worklet to destination...");
                this.workletNode.connect(this.audioContext.destination);

            } catch (err) {
                console.error("[Audio Engine] Initialization failed:", err);
                this.audioError = true;
                this.audioStatusMsg = "Erro ao inicializar";
                this.showToast("Falha ao iniciar áudio. Verifique o console.", "error");
            }
        },

        startMetricsPolling() {
            if (this.pollInterval) clearInterval(this.pollInterval);
            this.pollInterval = setInterval(() => {
                if (this.audioInitialized && this.workletNode) {
                    this.workletNode.port.postMessage({type: 'poll'});
                }
            }, 100);
        },

        // --- Transport Controls ---
        canPlay() {
            if (!this.audioInitialized) return false;
            if (this.previewMode === 'file') return !!this.audioBuffer;
            return true; // Synth can always play
        },

        handleModeSwitch() {
            this.stopPlay();
            if (this.previewMode === 'file' && !this.audioBuffer) {
                this.audioStatusMsg = "Pronto";
            }
        },

        async togglePlay() {
            if (!this.canPlay()) return;

            if (this.isPlaying) {
                this.pausePlay();
            } else {
                await this.startPlay();
            }
        },

        async startPlay() {
            console.log("[Transport] Starting playback...");
            if (this.audioContext.state === 'suspended') {
                await this.audioContext.resume();
                console.log("[Transport] AudioContext resumed.");
            } else {
                console.log("[Transport] AudioContext already running.");
            }

            // Clean up any old node
            if (this.sourceNode) {
                try { this.sourceNode.stop(); } catch(e) {}
                this.sourceNode.disconnect();
                this.sourceNode = null;
            }

            this.isPlaying = true;
            this.audioError = false;

            if (this.previewMode === 'synth') {
                console.log("[Transport] Mode: Synth Plucks");
                this.startSynthPlucks();
            } else if (this.previewMode === 'file' && this.audioBuffer) {
                console.log("[Transport] Mode: Custom File");
                this.startFilePlayback();
            } else {
                console.log("[Transport] Playback aborted. Missing source buffer or invalid mode.");
            }
        },

        pausePlay() {
            console.log("[Transport] Pausing playback...");
            if (this.previewMode === 'file' && this.sourceNode) {
                try { this.sourceNode.stop(); } catch(e) {}
                this.sourceNode.disconnect();
                this.sourceNode = null;
                // Save context time for resuming
                this.pauseTimeContext += this.audioContext.currentTime - this.startTimeContext;
                console.log("[Transport] Paused at:", this.pauseTimeContext);
            } else if (this.previewMode === 'synth') {
                if (this.sourceNode) { try { this.sourceNode.stop(); } catch(e) {} }
                if (this.gainNode) { try { this.gainNode.disconnect(); } catch(e) {} }
                this.sourceNode = null;
                this.gainNode = null;
            }

            this.isPlaying = false;
            this.audioStatusMsg = "Pausado";
            if (this.synthPluckTimer) {
                clearTimeout(this.synthPluckTimer);
                this.synthPluckTimer = null;
            }
            this.stopProgressAnimation();
        },

        stopPlay() {
            console.log("[Transport] Stopping playback...");
            if (this.sourceNode) {
                try { this.sourceNode.stop(); } catch(e) {}
                this.sourceNode.disconnect();
                this.sourceNode = null;
            }
            if (this.gainNode) {
                try { this.gainNode.disconnect(); } catch(e) {}
                this.gainNode = null;
            }

            this.isPlaying = false;
            this.currentTime = 0;
            this.seekTime = 0;
            this.pauseTimeContext = 0;
            this.startTimeContext = 0;
            this.audioStatusMsg = "Parado";
            if (this.synthPluckTimer) {
                clearTimeout(this.synthPluckTimer);
                this.synthPluckTimer = null;
            }
            this.stopProgressAnimation();
        },

        // --- Pluck Synth ---
        startSynthPlucks() {
            this.audioStatusMsg = "Tocando Synth Plucks";
            this.currentTime = 0;

            const schedulePluck = () => {
                if (!this.isPlaying || this.previewMode !== 'synth') return;

                const now = this.audioContext.currentTime;

                // Create new oscillator per pluck to avoid lifecycle issues
                const osc = this.audioContext.createOscillator();
                osc.type = 'sawtooth';
                osc.frequency.value = 220; // A3

                const gain = this.audioContext.createGain();
                gain.gain.setValueAtTime(0, now);
                gain.gain.linearRampToValueAtTime(0.8, now + 0.01); // Sharp attack
                gain.gain.exponentialRampToValueAtTime(0.001, now + 0.5); // Decay

                osc.connect(gain);
                gain.connect(this.workletNode);

                osc.start(now);
                osc.stop(now + 0.6); // Auto-stop to clear memory

                // Only keep reference to the last one so Stop works immediately
                this.sourceNode = osc;
                this.gainNode = gain;

                this.synthPluckTimer = setTimeout(schedulePluck, 1000); // Trigger every second
            };

            schedulePluck();
        },

        // --- File Playback ---
        startFilePlayback() {
            this.audioStatusMsg = "Tocando arquivo";

            // Re-create node (WebAudio nodes are single-use)
            this.sourceNode = this.audioContext.createBufferSource();
            this.sourceNode.buffer = this.audioBuffer;
            this.sourceNode.connect(this.workletNode);

            // Resume from pause time or seek time
            const offset = this.pauseTimeContext || this.currentTime;

            // If we are at the end, start over
            const startOffset = offset >= this.duration ? 0 : offset;
            if (offset >= this.duration) {
                this.currentTime = 0;
                this.pauseTimeContext = 0;
            }

            this.sourceNode.start(0, startOffset);
            this.startTimeContext = this.audioContext.currentTime;

            // Handle natural end of file
            this.sourceNode.onended = () => {
                // Check if ended naturally or due to pause/stop/seek
                if (this.isPlaying && !this.isDraggingSeek &&
                    (this.pauseTimeContext + this.audioContext.currentTime - this.startTimeContext) >= this.duration - 0.1) {
                    this.stopPlay();
                }
            };

            this.startProgressAnimation();
        },

        // --- Seek Progress ---
        startProgressAnimation() {
            if (this.animationFrameId) cancelAnimationFrame(this.animationFrameId);

            const animate = () => {
                if (this.isPlaying && this.previewMode === 'file' && !this.isDraggingSeek) {
                    const elapsed = this.audioContext.currentTime - this.startTimeContext;
                    this.currentTime = this.pauseTimeContext + elapsed;

                    if (this.currentTime > this.duration) {
                        this.currentTime = this.duration;
                    }
                    this.seekTime = this.currentTime; // Bind UI
                }
                this.animationFrameId = requestAnimationFrame(animate);
            };
            this.animationFrameId = requestAnimationFrame(animate);
        },

        stopProgressAnimation() {
            if (this.animationFrameId) {
                cancelAnimationFrame(this.animationFrameId);
                this.animationFrameId = null;
            }
        },

        handleSeekInput() {
            this.isDraggingSeek = true;
            this.currentTime = this.seekTime;
        },

        handleSeekChange() {
            console.log("[Transport] Seeking to:", this.seekTime);
            this.isDraggingSeek = false;
            this.currentTime = this.seekTime;
            this.pauseTimeContext = this.currentTime;

            if (this.isPlaying) {
                // Restart node at new position
                try { this.sourceNode.stop(); } catch(e) {}
                this.sourceNode.disconnect();
                this.startTimeContext = this.audioContext.currentTime;

                this.sourceNode = this.audioContext.createBufferSource();
                this.sourceNode.buffer = this.audioBuffer;
                this.sourceNode.connect(this.workletNode);
                this.sourceNode.start(0, this.pauseTimeContext);

                this.sourceNode.onended = () => {
                    if (this.isPlaying && !this.isDraggingSeek &&
                        (this.pauseTimeContext + this.audioContext.currentTime - this.startTimeContext) >= this.duration - 0.1) {
                        this.stopPlay();
                    }
                };
            }
        },

        formatTime(seconds) {
            if (!seconds || isNaN(seconds)) return "0:00";
            const m = Math.floor(seconds / 60);
            const s = Math.floor(seconds % 60);
            return `${m}:${s.toString().padStart(2, '0')}`;
        },

        // --- DSP Communication ---
        pushParamToAudio(key, val) {
            if (!this.audioInitialized || !this.workletNode) return;
            if (key in WASM_PARAM_ID_MAP) {
                this.workletNode.port.postMessage({ type: 'param', id: WASM_PARAM_ID_MAP[key], value: val });
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
        }
    }));
});
