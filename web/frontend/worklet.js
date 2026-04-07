class BubbleCloudProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ready = false;
    this.wasm = null;
    this.heap = null;
    this.ptrIn = 0;
    this.ptrL = 0;
    this.ptrR = 0;
    this.capacity = 0;
    this.lastMetricsFrame = 0;
    this.bypass = false;

    this.port.onmessage = (e) => {
      const data = e.data || {};
      if (data.type === 'param' && this.wasm) {
        this.wasm.setParam(data.paramId | 0, Number(data.value) || 0);
      } else if (data.type === 'bypass') {
        this.bypass = !!data.enabled;
      } else if (data.type === 'reset' && this.wasm) {
        this.wasm.reset();
      }
    };

    this.initWasm();
  }

  async initWasm() {
    try {
      importScripts('bubble_cloud_wasm.js');
      const factory = self.Module;
      if (typeof factory !== 'function') {
        throw new Error('WASM module factory not found');
      }
      const mod = await factory();

      this.wasm = {
        init: mod.cwrap('wasm_init', null, []),
        reset: mod.cwrap('wasm_reset', null, []),
        process: mod.cwrap('wasm_process', null, ['number', 'number', 'number', 'number']),
        setParam: mod.cwrap('wasm_set_param', null, ['number', 'number']),
        alloc: mod.cwrap('wasm_alloc', 'number', ['number']),
        free: mod.cwrap('wasm_free', null, ['number']),
        getEnvelope: mod.cwrap('wasm_get_envelope', 'number', []),
        getState: mod.cwrap('wasm_get_state', 'number', []),
        getVoices: mod.cwrap('wasm_get_active_voices', 'number', []),
      };

      this.heap = mod.HEAPF32;
      this.wasm.init();
      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    } catch (error) {
      this.port.postMessage({ type: 'wasm-error', error: String(error?.message || error) });
    }
  }

  ensureBuffers(frames) {
    if (this.capacity >= frames) return;
    if (this.ptrIn) this.wasm.free(this.ptrIn);
    if (this.ptrL) this.wasm.free(this.ptrL);
    if (this.ptrR) this.wasm.free(this.ptrR);
    const bytes = frames * 4;
    this.ptrIn = this.wasm.alloc(bytes);
    this.ptrL = this.wasm.alloc(bytes);
    this.ptrR = this.wasm.alloc(bytes);
    this.capacity = frames;
  }

  process(inputs, outputs) {
    const output = outputs[0];
    if (!output || output.length < 2) return true;
    const outL = output[0];
    const outR = output[1];
    const frames = outL.length;

    if (!this.ready || !this.wasm || this.bypass) {
      const in0 = inputs[0]?.[0];
      for (let i = 0; i < frames; i++) {
        const v = in0 ? in0[i] : 0;
        outL[i] = v;
        outR[i] = v;
      }
      return true;
    }

    this.ensureBuffers(frames);

    const in0 = inputs[0]?.[0];
    const inIndex = this.ptrIn >> 2;
    const lIndex = this.ptrL >> 2;
    const rIndex = this.ptrR >> 2;

    for (let i = 0; i < frames; i++) this.heap[inIndex + i] = in0 ? in0[i] : 0;

    this.wasm.process(this.ptrIn, this.ptrL, this.ptrR, frames);

    for (let i = 0; i < frames; i++) {
      outL[i] = this.heap[lIndex + i];
      outR[i] = this.heap[rIndex + i];
    }

    this.lastMetricsFrame += frames;
    if (this.lastMetricsFrame >= 2048) {
      this.lastMetricsFrame = 0;
      this.port.postMessage({
        type: 'metrics',
        envelope: this.wasm.getEnvelope(),
        state: this.wasm.getState(),
        voices: this.wasm.getVoices(),
      });
    }

    return true;
  }
}

registerProcessor('bubble-cloud-processor', BubbleCloudProcessor);
