import createBubbleCloudModule from './bubble_cloud_wasm.js';

class SoundBubblesWorklet extends AudioWorkletProcessor {
  constructor() {
    super();

    this.ready = false;
    this.bypass = false;
    this.metrics = { envelope: 0, state: 0, voices: 0 };

    this.wasm = null;
    this.wasmInit = null;
    this.wasmReset = null;
    this.wasmProcess = null;
    this.wasmSetParam = null;
    this.wasmAlloc = null;
    this.wasmFree = null;
    this.wasmGetEnvelope = null;
    this.wasmGetState = null;
    this.wasmGetVoices = null;

    this.bufferSize = 0;
    this.inPtr = 0;
    this.outLPtr = 0;
    this.outRPtr = 0;
    this.inHeap = null;
    this.outLHeap = null;
    this.outRHeap = null;

    this.port.onmessage = (event) => {
      const data = event.data || {};
      if (data.type === 'set-bypass') {
        this.bypass = !!data.enabled;
        return;
      }
      if (data.type === 'poll') {
        this.port.postMessage({ type: 'state', ...this.metrics });
        return;
      }
      if (!this.ready) return;
      if (data.type === 'param') {
        try {
          this.wasmSetParam(data.id | 0, Number(data.value));
        } catch (err) {
          this.postError('set_param failed', err);
        }
      }
      if (data.type === 'reset-engine') {
        try {
          this.wasmReset();
        } catch (err) {
          this.postError('reset failed', err);
        }
      }
    };

    this.initialize();
  }

  postError(prefix, err) {
    this.port.postMessage({
      type: 'processor-error',
      message: `${prefix}: ${err?.stack || err?.message || String(err)}`,
    });
  }

  ensureBuffers(blockSize) {
    if (this.bufferSize === blockSize && this.inPtr && this.outLPtr && this.outRPtr) {
      return;
    }

    if (this.inPtr) this.wasmFree(this.inPtr);
    if (this.outLPtr) this.wasmFree(this.outLPtr);
    if (this.outRPtr) this.wasmFree(this.outRPtr);

    const bytes = blockSize * Float32Array.BYTES_PER_ELEMENT;
    this.inPtr = this.wasmAlloc(bytes);
    this.outLPtr = this.wasmAlloc(bytes);
    this.outRPtr = this.wasmAlloc(bytes);

    if (!this.inPtr || !this.outLPtr || !this.outRPtr) {
      throw new Error(`WASM buffer allocation failed for blockSize=${blockSize}`);
    }

    this.bufferSize = blockSize;
    this.inHeap = new Float32Array(this.wasm.HEAPF32.buffer, this.inPtr, blockSize);
    this.outLHeap = new Float32Array(this.wasm.HEAPF32.buffer, this.outLPtr, blockSize);
    this.outRHeap = new Float32Array(this.wasm.HEAPF32.buffer, this.outRPtr, blockSize);
  }

  async initialize() {
    this.port.postMessage({ type: 'loading' });
    try {
      this.wasm = await createBubbleCloudModule();
      this.wasmInit = this.wasm.cwrap('wasm_init', null, []);
      this.wasmReset = this.wasm.cwrap('wasm_reset', null, []);
      this.wasmProcess = this.wasm.cwrap('wasm_process', null, ['number', 'number', 'number', 'number']);
      this.wasmSetParam = this.wasm.cwrap('wasm_set_param', null, ['number', 'number']);
      this.wasmAlloc = this.wasm.cwrap('wasm_alloc', 'number', ['number']);
      this.wasmFree = this.wasm.cwrap('wasm_free', null, ['number']);
      this.wasmGetEnvelope = this.wasm.cwrap('wasm_get_envelope', 'number', []);
      this.wasmGetState = this.wasm.cwrap('wasm_get_state', 'number', []);
      this.wasmGetVoices = this.wasm.cwrap('wasm_get_active_voices', 'number', []);

      this.wasmInit();
      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    } catch (err) {
      this.ready = false;
      this.port.postMessage({
        type: 'init-failed',
        message: err?.stack || err?.message || String(err),
      });
    }
  }

  process(inputs, outputs) {
    const input = inputs[0] || [];
    const output = outputs[0] || [];
    const inL = input[0];
    const inR = input[1];
    const outL = output[0];
    const outR = output[1] || output[0];

    if (!outL || !outR) return true;

    if (this.bypass) {
      for (let i = 0; i < outL.length; i += 1) {
        const sample = inL?.[i] ?? inR?.[i] ?? 0;
        outL[i] = sample;
        outR[i] = sample;
      }
      return true;
    }

    if (!this.ready) {
      outL.fill(0);
      outR.fill(0);
      return true;
    }

    try {
      const blockSize = outL.length;
      this.ensureBuffers(blockSize);

      this.inHeap.fill(0);
      if (inL) {
        this.inHeap.set(inL.subarray(0, blockSize));
      } else if (inR) {
        this.inHeap.set(inR.subarray(0, blockSize));
      }

      this.wasmProcess(this.inPtr, this.outLPtr, this.outRPtr, blockSize);

      outL.set(this.outLHeap);
      outR.set(this.outRHeap);

      this.metrics.envelope = this.wasmGetEnvelope();
      this.metrics.state = this.wasmGetState();
      this.metrics.voices = this.wasmGetVoices();
    } catch (err) {
      outL.fill(0);
      outR.fill(0);
      this.postError('process failed', err);
    }

    return true;
  }
}

registerProcessor('sound-bubbles-worklet', SoundBubblesWorklet);
