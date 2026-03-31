class SoundBubblesWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.wasmLoaded = false;
    this.bufferSize = 128; // Usually 128 in AudioWorklet

    // Pointers for WASM memory
    this.inPtr = 0;
    this.outLPtr = 0;
    this.outRPtr = 0;

    // We need to wait for the WASM module to be ready
    console.log("Worklet: Starting WASM load...", typeof BubbleCloudModule);
    BubbleCloudModule().then(module => {
      console.log("Worklet: WASM loaded!");
      this.wasm = module;
      this.wasm._wasm_init();

      // Allocate memory for I/O buffers in WASM heap
      const byteSize = this.bufferSize * 4; // 32-bit floats
      this.inPtr = this.wasm._wasm_alloc(byteSize);
      this.outLPtr = this.wasm._wasm_alloc(byteSize);
      this.outRPtr = this.wasm._wasm_alloc(byteSize);

      this.wasmLoaded = true;
      this.port.postMessage({ type: 'ready' });
    }).catch(e => {
      console.error("Worklet: WASM load failed", e);
    });

    // Handle parameter changes from main thread
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
              voices: this.wasm._wasm_get_active_voices()
           });
        }
      }
    };
  }

  process(inputs, outputs, parameters) {
    if (!this.wasmLoaded) return true;

    const input = inputs[0];
    const output = outputs[0];

    // We assume 1 channel in (or we mix down), 2 channels out
    if (!input || input.length === 0 || !input[0]) {
      // Silence if no input connected
      return true;
    }

    const inputChannel = input[0];
    const outputL = output[0];
    const outputR = output.length > 1 ? output[1] : output[0]; // fallback if output is mono

    // 1. Copy JS input buffer to WASM memory
    const wasmInputArray = new Float32Array(this.wasm.HEAPF32.buffer, this.inPtr, this.bufferSize);

    // If input is stereo, we could mix it down to mono here. For now, just take left channel.
    wasmInputArray.set(inputChannel);

    // 2. Process
    this.wasm._wasm_process(this.inPtr, this.outLPtr, this.outRPtr, this.bufferSize);

    // 3. Copy WASM output memory back to JS output buffers
    const wasmOutputLArray = new Float32Array(this.wasm.HEAPF32.buffer, this.outLPtr, this.bufferSize);
    const wasmOutputRArray = new Float32Array(this.wasm.HEAPF32.buffer, this.outRPtr, this.bufferSize);

    outputL.set(wasmOutputLArray);
    if (output.length > 1) {
       outputR.set(wasmOutputRArray);
    }

    return true;
  }
}

console.log("Registering worklet node...");
registerProcessor('sound-bubbles-worklet', SoundBubblesWorklet);
console.log("Registration completed.");
