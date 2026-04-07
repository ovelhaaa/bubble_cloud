class SoundBubblesWorklet extends AudioWorkletProcessor {
  constructor() {
    super();

    this.ready = false;
    this.bypass = false;
    this.metrics = { envelope: 0, state: 0, voices: 0 };

    this.wasm = null;
    this.api = null;

    this.bufferSize = 128;
    this.inPtr = 0;
    this.outLPtr = 0;
    this.outRPtr = 0;
    this._inHeap = null;
    this._outLHeap = null;
    this._outRHeap = null;

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

      if (data.type === 'param' && this.api) {
        try {
          this.api.setParam(data.id, data.value);
        } catch (err) {
          this.postProcessorError('setParam failed', err);
        }
      }
    };

    this.initialize();
  }

  postProcessorError(prefix, err) {
    this.port.postMessage({
      type: 'processor-error',
      message: `${prefix}: ${err?.stack || err?.message || String(err)}`,
    });
  }

  async initialize() {
    this.port.postMessage({ type: 'loading' });

    try {
      const wasmUrl = new URL('bubble_cloud_wasm.js', self.location.href).toString();
      importScripts(wasmUrl);

      if (typeof BubbleCloudModule !== 'function') {
        throw new Error('BubbleCloudModule factory not found after importScripts');
      }

      this.wasm = await BubbleCloudModule();

      const wasmInit = this.wasm.cwrap('wasm_init', null, []);
      const wasmProcess = this.wasm.cwrap('wasm_process', null, ['number', 'number', 'number', 'number']);
      const wasmSetParam = this.wasm.cwrap('wasm_set_param', null, ['number', 'number']);
      const wasmAlloc = this.wasm.cwrap('wasm_alloc', 'number', ['number']);
      const wasmGetEnvelope = this.wasm.cwrap('wasm_get_envelope', 'number', []);
      const wasmGetState = this.wasm.cwrap('wasm_get_state', 'number', []);
      const wasmGetActiveVoices = this.wasm.cwrap('wasm_get_active_voices', 'number', []);

      wasmInit();

      const byteSize = this.bufferSize * Float32Array.BYTES_PER_ELEMENT;
      this.inPtr = wasmAlloc(byteSize);
      this.outLPtr = wasmAlloc(byteSize);
      this.outRPtr = wasmAlloc(byteSize);

      if (!this.inPtr || !this.outLPtr || !this.outRPtr) {
        throw new Error('Failed to allocate WASM audio buffers');
      }

      this.api = {
        setParam: (id, value) => wasmSetParam(id | 0, Number(value)),
        processBlock: (input, output, metrics) => {
          const inL = input[0];
          const outL = output[0];
          const outR = output[1];

          if (!this._inHeap || this._inHeap.buffer !== this.wasm.HEAPF32.buffer) {
            const buffer = this.wasm.HEAPF32.buffer;
            const inHeap = new Float32Array(buffer, this.inPtr, this.bufferSize);
            const outLHeap = new Float32Array(buffer, this.outLPtr, this.bufferSize);
            const outRHeap = new Float32Array(buffer, this.outRPtr, this.bufferSize);

            this._inHeap = inHeap;
            this._outLHeap = outLHeap;
            this._outRHeap = outRHeap;
          }

          if (inL) {
            this._inHeap.set(inL);
          } else {
            this._inHeap.fill(0);
          }

          wasmProcess(this.inPtr, this.outLPtr, this.outRPtr, this.bufferSize);

          outL.set(this._outLHeap);
          if (outR) {
            outR.set(this._outRHeap);
          }

          metrics.envelope = wasmGetEnvelope();
          metrics.state = wasmGetState();
          metrics.voices = wasmGetActiveVoices();
        },
      };

      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    } catch (err) {
      this.port.postMessage({
        type: 'init-failed',
        message: err?.stack || err?.message || String(err),
      });
    }
  }

  process(inputs, outputs) {
    try {
      const input = inputs[0] || [];
      const output = outputs[0] || [];
      const inL = input[0];
      const outL = output[0];
      const outR = output[1] || output[0];

      if (!outL || !outR) {
        return true;
      }

      if (this.bypass) {
        if (inL) {
          for (let i = 0; i < outL.length; i++) {
            const sample = inL[i] || 0;
            outL[i] = sample;
            outR[i] = sample;
          }
        } else {
          outL.fill(0);
          outR.fill(0);
        }
        return true;
      }

      if (!this.ready || !this.api) {
        outL.fill(0);
        outR.fill(0);
        return true;
      }

      this.api.processBlock(input, output, this.metrics);
      return true;
    } catch (err) {
      this.postProcessorError('process() failed', err);
      return true;
    }
  }
}

registerProcessor('sound-bubbles-worklet', SoundBubblesWorklet);
