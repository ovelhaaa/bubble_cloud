importScripts('bubble_cloud_wasm.js');

class SoundBubblesWorklet extends AudioWorkletProcessor {
  constructor() {
    super();

    this.wasmLoaded = false;
    this.bufferSize = 128;

    this.inPtr = 0;
    this.outLPtr = 0;
    this.outRPtr = 0;

    console.log("Worklet: Starting WASM load...", typeof BubbleCloudModule);

    BubbleCloudModule()
      .then((module) => {
        console.log("Worklet: WASM loaded!");
        this.wasm = module;
        this.wasm._wasm_init();

        const byteSize = this.bufferSize * 4; // float32
        this.inPtr = this.wasm._wasm_alloc(byteSize);
        this.outLPtr = this.wasm._wasm_alloc(byteSize);
        this.outRPtr = this.wasm._wasm_alloc(byteSize);

        this.wasmLoaded = true;
        this.port.postMessage({ type: 'ready' });
      })
      .catch((e) => {
        console.error("Worklet: WASM load failed", e);
        this.port.postMessage({
          type: 'wasm-error',
          message: e?.message || String(e),
        });
      });

    this.port.onmessage = (event) => {
      if (event.data.type === 'param') {
        if (this.wasmLoaded) {
          this.wasm._wasm_set_param(event.data.id, event.data.value);
        }
      } else if (event.data.type === 'poll') {
        if (this.wasmLoaded) {
          this.port.postMessage({
            type: 'state',
            envelope: this.wasm._wasm_get_envelope(),
            state: this.wasm._wasm_get_state(),
            voices: this.wasm._wasm_get_active_voices(),
          });
        }
      }
    };
  }

  updateViews() {
    if (this.wasmLoaded && this.wasm.HEAPF32.buffer.byteLength > 0) {
      if (!this.inView || this.inView.buffer !== this.wasm.HEAPF32.buffer) {
        this.inView = new Float32Array(this.wasm.HEAPF32.buffer, this.inPtr, this.bufferSize);
        this.outLView = new Float32Array(this.wasm.HEAPF32.buffer, this.outLPtr, this.bufferSize);
        this.outRView = new Float32Array(this.wasm.HEAPF32.buffer, this.outRPtr, this.bufferSize);
      }
    }
  }

  process(inputs, outputs) {
    if (!this.wasmLoaded) return true;

    const input = inputs[0];
    const output = outputs[0];

    if (!output || output.length === 0) return true;
    if (!input || input.length === 0 || !input[0]) return true;

    this.updateViews();
    if (!this.inView) return true;

    const inputL = input[0];
    const inputR = input.length > 1 ? input[1] : undefined;

    const outputL = output[0];
    const outputR = output.length > 1 ? output[1] : output[0];

    // 1) Mix down para mono e copia o input JS para a memória WASM
    if (inputR) {
      for (let i = 0; i < this.bufferSize; i++) {
        this.inView[i] = (inputL[i] + inputR[i]) * 0.5;
      }
    } else {
      this.inView.set(inputL);
    }

    // 2) Processa no WASM
    this.wasm._wasm_process(this.inPtr, this.outLPtr, this.outRPtr, this.bufferSize);

    // 3) Copia a saída WASM de volta para os buffers JS
    outputL.set(this.outLView);
    if (output.length > 1) {
      outputR.set(this.outRView);
    }

    return true;
  }
}

console.log("Registering worklet node...");
registerProcessor('sound-bubbles-worklet', SoundBubblesWorklet);
console.log("Registration completed.");