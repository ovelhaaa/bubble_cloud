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
  mix_wet_gain: 0.5,
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
  mix_wet_gain: 26,
};

document.addEventListener('alpine:init', () => {
  Alpine.data('editorApp', () => ({
    params: { ...BASELINE },
    audioInitialized: false,
    audioContext: null,
    workletNode: null,
    workletModuleLoaded: false,
    audioStatusMsg: 'Não iniciado',
    audioError: false,
    audioDiagnostics: [],
    debugBypassEnabled: false,
    metrics: { envelope: 0, state: 0, voices: 0 },
    pollInterval: null,

    logAudio(step, detail = null, isError = false) {
      const message = detail ? `${step}: ${detail}` : step;
      if (isError) {
        console.error('[Audio Engine]', message, detail || '');
      } else {
        console.log('[Audio Engine]', message, detail || '');
      }
      this.audioDiagnostics.push({ ts: Date.now(), step, detail, isError });
      if (this.audioDiagnostics.length > 100) this.audioDiagnostics.shift();
    },

    setAudioStatus(message, isError = false) {
      this.audioStatusMsg = message;
      this.audioError = isError;
      this.logAudio('status', message, isError);
    },

    async initAudio() {
      try {
        this.logAudio('initAudio', 'User gesture start');
        this.logAudio('userActivation', `isActive=${navigator.userActivation?.isActive}, hasBeenActive=${navigator.userActivation?.hasBeenActive}`);
        this.setAudioStatus('Inicializando AudioContext...');

        if (!this.audioContext) {
          this.logAudio('AudioContext', 'Creating AudioContext');
          this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
          this.logAudio('AudioContext', `Created with state=${this.audioContext.state}`);
        }

        this.logAudio('AudioContext.resume', `Before resume state=${this.audioContext.state}`);
        if (this.audioContext.state === 'suspended') {
          await this.audioContext.resume();
        }
        this.logAudio('AudioContext.resume', `After resume state=${this.audioContext.state}`);

        this.setAudioStatus('Carregando AudioWorklet...');
        const workletUrl = new URL('worklet.js', window.location.href).toString();
        if (!this.workletModuleLoaded) {
          this.logAudio('audioWorklet.addModule', `Loading ${workletUrl}`);
          await this.audioContext.audioWorklet.addModule(workletUrl);
          this.workletModuleLoaded = true;
          this.logAudio('audioWorklet.addModule', 'Module loaded');
        } else {
          this.logAudio('audioWorklet.addModule', 'Module already loaded, skipping');
        }

        this.setAudioStatus('Criando AudioWorkletNode...');
        if (this.workletNode) {
          try {
            this.workletNode.disconnect();
          } catch (disconnectErr) {
            this.logAudio('AudioWorkletNode', `Previous node disconnect failed: ${disconnectErr?.message || disconnectErr}`);
          }
        }
        this.workletNode = new AudioWorkletNode(this.audioContext, 'sound-bubbles-worklet', {
          outputChannelCount: [2],
        });
        this.logAudio('AudioWorkletNode', 'Node created');

        this.workletNode.onprocessorerror = (event) => {
          const msg = `Processor error: ${event?.message || 'unknown processor failure'}`;
          this.logAudio('onprocessorerror', msg, true);
          this.setAudioStatus(`Erro no processor: ${msg}`, true);
        };

        this.workletNode.port.onmessage = (e) => {
          this.logAudio('port.onmessage', JSON.stringify(e.data));
          const data = e.data || {};
          if (data.type === 'ready') {
            this.audioInitialized = true;
            this.setAudioStatus('Audio engine ready');
            this.pushAllParamsToAudio();
            this.startMetricsPolling();
          } else if (data.type === 'loading') {
            this.setAudioStatus('Loading audio engine...');
          } else if (data.type === 'state') {
            this.metrics.envelope = data.envelope;
            this.metrics.state = data.state;
            this.metrics.voices = data.voices;
          } else if (data.type === 'wasm-error' || data.type === 'init-failed' || data.type === 'processor-error') {
            const message = data.message || 'unknown failure';
            this.setAudioStatus(`WASM/Worklet failure: ${message}`, true);
          }
        };

        this.logAudio('connect', 'Connecting worklet node to destination');
        this.workletNode.connect(this.audioContext.destination);
        this.logAudio('connect', 'Worklet connected');

        this.workletNode.port.postMessage({ type: 'set-bypass', enabled: this.debugBypassEnabled });
      } catch (err) {
        const detail = err?.stack || err?.message || String(err);
        this.logAudio('initAudio failed', detail, true);
        this.setAudioStatus(`Erro ao inicializar: ${detail}`, true);
      }
    },

    setDebugBypass(enabled) {
      this.debugBypassEnabled = !!enabled;
      this.logAudio('setDebugBypass', `enabled=${this.debugBypassEnabled}`);
      if (this.workletNode) {
        this.workletNode.port.postMessage({ type: 'set-bypass', enabled: this.debugBypassEnabled });
      }
    },

    startMetricsPolling() {
      if (this.pollInterval) clearInterval(this.pollInterval);
      this.pollInterval = setInterval(() => {
        if (this.audioInitialized && this.workletNode) {
          this.workletNode.port.postMessage({ type: 'poll' });
        }
      }, 100);
    },

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
  }));
});