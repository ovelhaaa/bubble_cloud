const STORAGE_KEY = 'bubble_cloud_web_draft_v1';

const BASELINE = {
  noise_floor: 0.001,
  tracking_thresh: 0.01,
  sustain_thresh: 0.05,
  transient_delta: 0.05,
  duck_burst_level: 0.2,
  duck_attack_coef: 0.8,
  duck_release_coef: 0.99,
  burst_duration_ticks: 10,
  burst_immediate_count: 3,
  density_burst: 50,
  density_sustain: 15,
  density_decay: 5,
  attack_region_min_offset_samples: 441,
  attack_region_max_offset_samples: 3528,
  body_region_min_offset_samples: 3528,
  body_region_max_offset_samples: 11025,
  memory_region_min_offset_samples: 11025,
  memory_region_max_offset_samples: 39690,
  micro_duration_ms_min: 5,
  micro_duration_ms_max: 15,
  short_duration_ms_min: 20,
  short_duration_ms_max: 50,
  body_duration_ms_min: 80,
  body_duration_ms_max: 200,
  rng_seed: 1,
  mix_dry_gain: 0.5,
  mix_wet_gain: 0.5,
};

const PARAM_DEFINITIONS = {
  noise_floor: { min: 0, max: 0.1, step: 0.0005, label: 'Noise Floor' },
  tracking_thresh: { min: 0, max: 0.2, step: 0.001, label: 'Tracking Threshold' },
  sustain_thresh: { min: 0, max: 0.5, step: 0.001, label: 'Sustain Threshold' },
  transient_delta: { min: 0, max: 0.5, step: 0.001, label: 'Transient Delta' },
  duck_burst_level: { min: 0, max: 1, step: 0.01, label: 'Duck Burst Level' },
  duck_attack_coef: { min: 0, max: 1, step: 0.001, label: 'Duck Attack Coef' },
  duck_release_coef: { min: 0.8, max: 0.9999, step: 0.0001, label: 'Duck Release Coef' },
  burst_duration_ticks: { min: 1, max: 64, step: 1, label: 'Burst Duration Ticks' },
  burst_immediate_count: { min: 1, max: 12, step: 1, label: 'Burst Immediate Count' },
  density_burst: { min: 0, max: 200, step: 0.1, label: 'Burst Density' },
  density_sustain: { min: 0, max: 100, step: 0.1, label: 'Sustain Density' },
  density_decay: { min: 0, max: 50, step: 0.1, label: 'Density Decay' },
  attack_region_min_offset_samples: { min: 0, max: 30000, step: 1, label: 'Attack Min Offset' },
  attack_region_max_offset_samples: { min: 64, max: 60000, step: 1, label: 'Attack Max Offset' },
  body_region_min_offset_samples: { min: 128, max: 90000, step: 1, label: 'Body Min Offset' },
  body_region_max_offset_samples: { min: 512, max: 120000, step: 1, label: 'Body Max Offset' },
  memory_region_min_offset_samples: { min: 512, max: 150000, step: 1, label: 'Memory Min Offset' },
  memory_region_max_offset_samples: { min: 1024, max: 220500, step: 1, label: 'Memory Max Offset' },
  micro_duration_ms_min: { min: 1, max: 100, step: 0.1, label: 'Micro Duration Min' },
  micro_duration_ms_max: { min: 1, max: 200, step: 0.1, label: 'Micro Duration Max' },
  short_duration_ms_min: { min: 1, max: 200, step: 0.1, label: 'Short Duration Min' },
  short_duration_ms_max: { min: 1, max: 300, step: 0.1, label: 'Short Duration Max' },
  body_duration_ms_min: { min: 10, max: 500, step: 0.1, label: 'Body Duration Min' },
  body_duration_ms_max: { min: 20, max: 800, step: 0.1, label: 'Body Duration Max' },
  rng_seed: { min: 1, max: 2147483647, step: 1, label: 'RNG Seed' },
  mix_dry_gain: { min: 0, max: 1, step: 0.01, label: 'Dry Gain' },
  mix_wet_gain: { min: 0, max: 1, step: 0.01, label: 'Wet Gain' },
};

const PARAM_HELP = {
  noise_floor: { musical: 'Filtra micro ruído na entrada para evitar grãos acidentais.', technical: 'Valor mínimo de energia para considerar sinal útil.' },
  tracking_thresh: { musical: 'Ajusta sensibilidade geral do detector.', technical: 'Threshold principal para tracking de envelope.' },
  sustain_thresh: { musical: 'Define quando o motor considera que entrou em sustain.', technical: 'Faixa de histerese para estado de sustentação.' },
  transient_delta: { musical: 'Controla quão agressivo é detectar ataques.', technical: 'Delta mínimo entre amostras para marcar transiente.' },
  duck_burst_level: { musical: 'Quanto o burst inicial derruba o nível base.', technical: 'Profundidade do duck durante transientes.' },
  duck_attack_coef: { musical: 'Velocidade de entrada do duck.', technical: 'Coeficiente exponencial de ataque do duck.' },
  duck_release_coef: { musical: 'Tempo para recuperar após transiente.', technical: 'Coeficiente exponencial de release do duck.' },
  burst_duration_ticks: { musical: 'Quanto tempo dura o modo burst.', technical: 'Contador de ticks após detecção de ataque.' },
  burst_immediate_count: { musical: 'Número de grãos imediatos no ataque.', technical: 'Vozes disparadas instantaneamente em transiente.' },
  density_burst: { musical: 'Densidade de grãos no ataque.', technical: 'Taxa de spawn durante estado burst.' },
  density_sustain: { musical: 'Densidade de grãos no corpo/sustain.', technical: 'Taxa de spawn no estado sustentado.' },
  density_decay: { musical: 'Decaimento da densidade ao sair de sustain.', technical: 'Redução por bloco para voltar ao repouso.' },
  attack_region_min_offset_samples: { musical: 'Início da leitura para material recente.', technical: 'Offset mínimo em samples da região de ataque.' },
  attack_region_max_offset_samples: { musical: 'Fim da janela de ataque.', technical: 'Offset máximo em samples da região de ataque.' },
  body_region_min_offset_samples: { musical: 'Início da leitura da região de corpo.', technical: 'Offset mínimo da janela de sustain.' },
  body_region_max_offset_samples: { musical: 'Fim da leitura da região de corpo.', technical: 'Offset máximo da janela de sustain.' },
  memory_region_min_offset_samples: { musical: 'Começo da memória longa.', technical: 'Offset mínimo da região histórica.' },
  memory_region_max_offset_samples: { musical: 'Limite da memória longa.', technical: 'Offset máximo da região histórica.' },
  micro_duration_ms_min: { musical: 'Menor duração de micro bolhas.', technical: 'Duração mínima (ms) da classe micro.' },
  micro_duration_ms_max: { musical: 'Maior duração de micro bolhas.', technical: 'Duração máxima (ms) da classe micro.' },
  short_duration_ms_min: { musical: 'Menor duração de bolhas curtas.', technical: 'Duração mínima (ms) da classe short.' },
  short_duration_ms_max: { musical: 'Maior duração de bolhas curtas.', technical: 'Duração máxima (ms) da classe short.' },
  body_duration_ms_min: { musical: 'Menor duração das bolhas de corpo.', technical: 'Duração mínima (ms) da classe body.' },
  body_duration_ms_max: { musical: 'Maior duração das bolhas de corpo.', technical: 'Duração máxima (ms) da classe body.' },
  rng_seed: { musical: 'Troca o “caráter” aleatório do preset.', technical: 'Seed do gerador pseudoaleatório do DSP.' },
  mix_dry_gain: { musical: 'Volume do sinal limpo.', technical: 'Ganho linear aplicado ao dry path.' },
  mix_wet_gain: { musical: 'Volume do sinal processado.', technical: 'Ganho linear aplicado ao wet path.' },
};

const PARAMETER_GROUPS = [
  { name: 'Detector & Dynamics', params: ['noise_floor', 'tracking_thresh', 'sustain_thresh', 'transient_delta', 'duck_burst_level', 'duck_attack_coef', 'duck_release_coef'] },
  { name: 'Burst & Density', params: ['burst_duration_ticks', 'burst_immediate_count', 'density_burst', 'density_sustain', 'density_decay'] },
  { name: 'Read Regions (samples)', params: ['attack_region_min_offset_samples', 'attack_region_max_offset_samples', 'body_region_min_offset_samples', 'body_region_max_offset_samples', 'memory_region_min_offset_samples', 'memory_region_max_offset_samples'] },
  { name: 'Grain Durations (ms)', params: ['micro_duration_ms_min', 'micro_duration_ms_max', 'short_duration_ms_min', 'short_duration_ms_max', 'body_duration_ms_min', 'body_duration_ms_max'] },
  { name: 'Mix & Random', params: ['rng_seed', 'mix_dry_gain', 'mix_wet_gain'] },
];

const WASM_PARAM_ID_MAP = Object.freeze({
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
});

function clamp(value, min, max) {
  if (Number.isNaN(value)) return min;
  return Math.min(max, Math.max(min, value));
}

document.addEventListener('alpine:init', () => {
  Alpine.data('editorApp', () => ({
    params: { ...BASELINE },
    baseline: { ...BASELINE },
    parameterGroups: PARAMETER_GROUPS,
    paramDefinitions: PARAM_DEFINITIONS,
    paramHelp: PARAM_HELP,

    openAccordion: PARAMETER_GROUPS[0].name,
    openHelp: {},

    toasts: [],
    showResetModal: false,
    isDraft: false,
    hasUnexportedChanges: false,
    parityNote: 'Paridade sonora: mesma engine DSP para modo synth e modo file.',

    audioInitialized: false,
    audioContext: null,
    workletNode: null,
    sourceBus: null,
    synthOscillator: null,
    synthGain: null,
    workletModuleLoaded: false,
    workletReady: false,

    audioStatusMsg: 'Não iniciado',
    audioError: false,

    previewMode: 'file',
    isPlaying: false,
    dragOver: false,

    audioFile: null,
    audioBuffer: null,
    isDecoding: false,

    fileSourceNode: null,
    startedAt: 0,
    pausedAt: 0,
    currentTime: 0,
    seekTime: 0,
    duration: 0,

    metrics: { envelope: 0, state: 0, voices: 0 },
    pollInterval: null,
    transportTimer: null,

    saveQueued: false,

    init() {
      this.restoreDraft();
      this.validateParamRanges();
      this.pushAllParamsToAudio();
    },

    setAudioStatus(message, isError = false) {
      this.audioStatusMsg = message;
      this.audioError = !!isError;
      if (isError) console.error('[audio]', message);
      else console.log('[audio]', message);
    },

    toast(message, type = 'info') {
      const id = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
      this.toasts.push({ id, message, type });
      setTimeout(() => {
        this.toasts = this.toasts.filter((t) => t.id !== id);
      }, 2800);
    },

    toggleAccordion(name) {
      this.openAccordion = this.openAccordion === name ? '' : name;
    },

    toggleHelp(paramKey) {
      this.openHelp[paramKey] = !this.openHelp[paramKey];
    },

    getParamConfig(paramKey) {
      return this.paramDefinitions[paramKey] || { min: 0, max: 1, step: 0.01 };
    },

    formatLabel(paramKey) {
      return this.paramDefinitions[paramKey]?.label || paramKey.replaceAll('_', ' ');
    },

    validateParamRanges() {
      Object.keys(this.params).forEach((key) => {
        const cfg = this.getParamConfig(key);
        this.params[key] = clamp(Number(this.params[key]), cfg.min, cfg.max);
      });
    },

    saveDraft() {
      this.validateParamRanges();
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ params: this.params, savedAt: Date.now() }));
      this.isDraft = true;
      this.hasUnexportedChanges = true;
      this.queueParamFlush();
    },

    restoreDraft() {
      try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) return;
        const parsed = JSON.parse(raw);
        if (parsed?.params && typeof parsed.params === 'object') {
          this.params = { ...this.params, ...parsed.params };
          this.isDraft = true;
          this.hasUnexportedChanges = true;
        }
      } catch (err) {
        this.toast(`Falha ao restaurar rascunho: ${err.message || err}`, 'error');
      }
    },

    clearDraft() {
      localStorage.removeItem(STORAGE_KEY);
      this.isDraft = false;
      this.hasUnexportedChanges = false;
    },

    confirmReset() {
      this.params = { ...this.baseline };
      this.validateParamRanges();
      this.clearDraft();
      this.showResetModal = false;
      this.queueParamFlush();
      if (this.workletNode) {
        this.workletNode.port.postMessage({ type: 'reset-engine' });
      }
      this.toast('Preset restaurado para o baseline.', 'success');
    },

    exportPayload() {
      return {
        meta: {
          app: 'bubble_cloud_web',
          exportedAt: new Date().toISOString(),
          mode: this.previewMode,
        },
        params: this.params,
      };
    },

    async copyJson() {
      const payload = JSON.stringify(this.exportPayload(), null, 2);
      try {
        await navigator.clipboard.writeText(payload);
        this.hasUnexportedChanges = false;
        this.toast('Preset copiado para a área de transferência.', 'success');
      } catch (err) {
        this.toast(`Falha ao copiar JSON: ${err.message || err}`, 'error');
      }
    },

    downloadJson() {
      try {
        const blob = new Blob([JSON.stringify(this.exportPayload(), null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement('a');
        anchor.href = url;
        anchor.download = `bubble-cloud-preset-${new Date().toISOString().replace(/[\W:]/g, '-')}.json`;
        anchor.click();
        URL.revokeObjectURL(url);
        this.hasUnexportedChanges = false;
        this.toast('Preset exportado com sucesso.', 'success');
      } catch (err) {
        this.toast(`Falha ao baixar JSON: ${err.message || err}`, 'error');
      }
    },

    async initAudio() {
      try {
        this.setAudioStatus('Inicializando contexto de áudio...');
        if (!this.audioContext) {
          this.audioContext = new AudioContext();
        }

        if (this.audioContext.state === 'suspended') {
          await this.audioContext.resume();
        }

        if (!this.workletModuleLoaded) {
          this.setAudioStatus('Carregando AudioWorklet...');
          await this.audioContext.audioWorklet.addModule(new URL('worklet.js', window.location.href).toString());
          this.workletModuleLoaded = true;
        }

        if (this.workletNode) {
          this.workletNode.disconnect();
        }

        this.workletNode = new AudioWorkletNode(this.audioContext, 'sound-bubbles-worklet', { outputChannelCount: [2] });
        this.sourceBus = new GainNode(this.audioContext, { gain: 1 });
        this.sourceBus.connect(this.workletNode);
        this.workletNode.connect(this.audioContext.destination);

        this.workletNode.onprocessorerror = (event) => {
          const message = event?.message || 'processor error';
          this.setAudioStatus(`Erro no AudioWorklet: ${message}`, true);
          this.toast(this.audioStatusMsg, 'error');
        };

        this.workletNode.port.onmessage = (event) => {
          const data = event.data || {};
          if (data.type === 'loading') {
            this.setAudioStatus('Inicializando WASM...');
          } else if (data.type === 'ready') {
            this.audioInitialized = true;
            this.workletReady = true;
            this.setAudioStatus('Áudio pronto');
            this.pushAllParamsToAudio();
            this.startMetricsPolling();
            this.toast('Engine inicializado com sucesso.', 'success');
          } else if (data.type === 'state') {
            this.metrics.envelope = Number(data.envelope) || 0;
            this.metrics.state = Number(data.state) || 0;
            this.metrics.voices = Number(data.voices) || 0;
          } else if (data.type === 'init-failed' || data.type === 'wasm-error' || data.type === 'processor-error') {
            this.workletReady = false;
            this.setAudioStatus(data.message || 'Falha na inicialização do WASM.', true);
            this.toast(this.audioStatusMsg, 'error');
          }
        };
      } catch (err) {
        this.setAudioStatus(`Falha ao iniciar áudio: ${err.message || err}`, true);
        this.toast(this.audioStatusMsg, 'error');
      }
    },

    pushParamToAudio(key) {
      if (!this.workletReady || !this.workletNode) return;
      const id = WASM_PARAM_ID_MAP[key];
      if (id === undefined) return;
      this.workletNode.port.postMessage({ type: 'param', id, value: Number(this.params[key]) });
    },

    pushAllParamsToAudio() {
      Object.keys(this.params).forEach((key) => this.pushParamToAudio(key));
    },

    queueParamFlush() {
      if (this.saveQueued) return;
      this.saveQueued = true;
      requestAnimationFrame(() => {
        this.saveQueued = false;
        this.pushAllParamsToAudio();
      });
    },

    startMetricsPolling() {
      clearInterval(this.pollInterval);
      this.pollInterval = setInterval(() => {
        if (this.workletNode && this.workletReady) {
          this.workletNode.port.postMessage({ type: 'poll' });
        }
      }, 100);
    },

    handleModeSwitch() {
      this.stopPlay();
      if (this.previewMode === 'synth') {
        this.duration = 0;
        this.currentTime = 0;
        this.seekTime = 0;
      } else if (this.audioBuffer) {
        this.duration = this.audioBuffer.duration;
      }
      this.setAudioStatus(this.previewMode === 'synth' ? 'Modo synth selecionado.' : 'Modo arquivo selecionado.');
    },

    canPlay() {
      if (!this.audioInitialized || !this.workletReady) return false;
      return this.previewMode === 'synth' ? true : !!this.audioBuffer;
    },

    togglePlay() {
      if (!this.canPlay()) return;
      if (this.isPlaying) {
        this.pausePlay();
      } else if (this.previewMode === 'synth') {
        this.startSynth();
      } else {
        this.startFilePlayback(this.pausedAt || 0);
      }
    },

    stopPlay() {
      if (this.previewMode === 'synth') {
        this.stopSynth();
      } else {
        this.stopFilePlayback(true);
      }
      this.isPlaying = false;
      this.stopTransportTimer();
      this.currentTime = 0;
      this.seekTime = 0;
      this.pausedAt = 0;
      if (this.previewMode === 'file' && this.audioBuffer) {
        this.duration = this.audioBuffer.duration;
      }
      this.setAudioStatus('Parado.');
    },

    pausePlay() {
      if (this.previewMode === 'synth') {
        this.stopSynth();
        this.isPlaying = false;
        this.stopTransportTimer();
        this.setAudioStatus('Synth pausado.');
        return;
      }
      this.stopFilePlayback(false);
      this.isPlaying = false;
      this.stopTransportTimer();
      this.setAudioStatus('Pausado.');
    },

    startSynth() {
      if (!this.audioContext || !this.sourceBus) return;
      this.stopSynth();
      this.synthOscillator = new OscillatorNode(this.audioContext, { type: 'sawtooth', frequency: 220 });
      this.synthGain = new GainNode(this.audioContext, { gain: 0 });
      this.synthOscillator.connect(this.synthGain).connect(this.sourceBus);
      const now = this.audioContext.currentTime;
      this.synthGain.gain.cancelScheduledValues(now);
      this.synthGain.gain.setValueAtTime(0, now);
      this.synthGain.gain.linearRampToValueAtTime(0.25, now + 0.02);
      this.synthOscillator.start();
      this.isPlaying = true;
      this.startedAt = now;
      this.setAudioStatus('Synth tocando.');
      this.startTransportTimer();
    },

    stopSynth() {
      const now = this.audioContext?.currentTime || 0;
      if (this.synthGain) {
        this.synthGain.gain.cancelScheduledValues(now);
        this.synthGain.gain.setTargetAtTime(0, now, 0.015);
      }
      if (this.synthOscillator) {
        try {
          this.synthOscillator.stop(now + 0.05);
        } catch (_) {}
        this.synthOscillator.disconnect();
        this.synthOscillator = null;
      }
      if (this.synthGain) {
        this.synthGain.disconnect();
        this.synthGain = null;
      }
    },

    startFilePlayback(offsetSeconds) {
      if (!this.audioBuffer || !this.sourceBus || !this.audioContext) return;
      this.stopFilePlayback(false);

      const source = new AudioBufferSourceNode(this.audioContext, { buffer: this.audioBuffer });
      source.connect(this.sourceBus);
      source.onended = () => {
        if (!this.fileSourceNode) return;
        this.fileSourceNode = null;
        this.isPlaying = false;
        this.stopTransportTimer();
        this.pausedAt = 0;
        this.currentTime = this.duration;
        this.seekTime = this.duration;
      };

      const safeOffset = clamp(offsetSeconds || 0, 0, this.audioBuffer.duration);
      source.start(0, safeOffset);
      this.fileSourceNode = source;
      this.startedAt = this.audioContext.currentTime - safeOffset;
      this.pausedAt = safeOffset;
      this.isPlaying = true;
      this.duration = this.audioBuffer.duration;
      this.setAudioStatus('Arquivo tocando.');
      this.startTransportTimer();
    },

    stopFilePlayback(resetOffset) {
      if (this.fileSourceNode) {
        try {
          this.fileSourceNode.onended = null;
          this.fileSourceNode.stop();
        } catch (_) {}
        this.fileSourceNode.disconnect();
        this.fileSourceNode = null;
      }
      if (!this.audioContext) return;
      if (resetOffset) {
        this.pausedAt = 0;
      } else {
        this.pausedAt = clamp(this.audioContext.currentTime - this.startedAt, 0, this.duration || Infinity);
      }
    },

    startTransportTimer() {
      this.stopTransportTimer();
      const tick = () => {
        if (!this.isPlaying || !this.audioContext) return;
        if (this.previewMode === 'file') {
          this.currentTime = clamp(this.audioContext.currentTime - this.startedAt, 0, this.duration || 0);
          this.seekTime = this.currentTime;
        }
        this.transportTimer = requestAnimationFrame(tick);
      };
      this.transportTimer = requestAnimationFrame(tick);
    },

    stopTransportTimer() {
      if (this.transportTimer) {
        cancelAnimationFrame(this.transportTimer);
        this.transportTimer = null;
      }
    },

    handleSeekInput() {
      if (this.previewMode !== 'file') return;
      this.currentTime = this.seekTime;
    },

    handleSeekChange() {
      if (this.previewMode !== 'file' || !this.audioBuffer) return;
      const target = clamp(Number(this.seekTime), 0, this.audioBuffer.duration);
      this.currentTime = target;
      this.pausedAt = target;
      if (this.isPlaying) {
        this.startFilePlayback(target);
      }
    },

    async handleFileSelect(event) {
      const file = event?.target?.files?.[0];
      if (!file) return;
      await this.loadFile(file);
    },

    async handleFileDrop(event) {
      this.dragOver = false;
      const file = event?.dataTransfer?.files?.[0];
      if (!file) return;
      await this.loadFile(file);
    },

    async loadFile(file) {
      if (!this.audioInitialized || !this.audioContext) {
        this.toast('Ative o áudio antes de carregar um arquivo.', 'error');
        return;
      }

      try {
        this.isDecoding = true;
        this.stopPlay();
        this.audioFile = file;
        const arrayBuffer = await file.arrayBuffer();
        this.audioBuffer = await this.audioContext.decodeAudioData(arrayBuffer.slice(0));
        this.duration = this.audioBuffer.duration;
        this.currentTime = 0;
        this.seekTime = 0;
        this.pausedAt = 0;
        this.previewMode = 'file';
        this.setAudioStatus(`Arquivo carregado: ${file.name}`);
        this.toast(`Arquivo pronto (${this.formatTime(this.duration)}).`, 'success');
      } catch (err) {
        this.audioBuffer = null;
        this.duration = 0;
        this.currentTime = 0;
        this.seekTime = 0;
        this.setAudioStatus(`Falha ao decodificar arquivo: ${err.message || err}`, true);
        this.toast(this.audioStatusMsg, 'error');
      } finally {
        this.isDecoding = false;
      }
    },

    clearAudioFile() {
      this.stopPlay();
      this.audioFile = null;
      this.audioBuffer = null;
      this.duration = 0;
      this.currentTime = 0;
      this.seekTime = 0;
      const input = document.getElementById('audio-upload');
      if (input) input.value = '';
      this.setAudioStatus('Arquivo removido.');
    },

    getEngineStateName(state) {
      const names = {
        0: 'Idle',
        1: 'Tracking',
        2: 'Sustain',
        3: 'Transient',
      };
      return names[state] || `State ${state}`;
    },

    formatTime(seconds) {
      const safe = Math.max(0, Number(seconds) || 0);
      const min = Math.floor(safe / 60);
      const sec = Math.floor(safe % 60);
      return `${min}:${String(sec).padStart(2, '0')}`;
    },
  }));
});
