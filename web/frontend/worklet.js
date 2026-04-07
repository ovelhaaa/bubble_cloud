class SoundBubblesWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ready = false;
    this.bypass = false;
    this.metrics = { envelope: 0, state: 0, voices: 0 };
    this.wasm = null;

    this.port.onmessage = (event) => {
      // Message bridge from UI/main thread.

      const data = event.data || {};
      if (data.type === 'set-bypass') {
        this.bypass = !!data.enabled;
      } else if (data.type === 'poll') {
        this.port.postMessage({ type: 'state', ...this.metrics });
      } else if (data.type === 'param') {
        if (this.wasm && typeof this.wasm.setParam === 'function') {
          try {
            this.wasm.setParam(data.id, data.value);
          } catch (err) {
            this.postProcessorError('setParam failed', err);
          }
        }
      }
    };

    this.initialize();
  }

  async initialize() {
    this.port.postMessage({ type: 'loading' });
    try {
      const wasmUrl = new URL('bubble_cloud_wasm.js', self.location.href).toString();
      importScripts(wasmUrl);
    } catch (err) {
      this.port.postMessage({
        type: 'wasm-error',
        message: err?.stack || err?.message || String(err),
      });
      return;
    }

    try {
      // Supports both module factory and global module patterns.
      if (typeof Module === 'function') {
        this.wasm = await Module();
      } else if (self.Module && typeof self.Module.then === 'function') {
        this.wasm = await self.Module;
      } else {
        this.wasm = self.Module || null;
      }

      this.ready = true;
      this.port.postMessage({ type: 'ready' });
    } catch (err) {
      this.port.postMessage({
        type: 'init-failed',
        message: err?.stack || err?.message || String(err),
      });
    }
  }

  postProcessorError(prefix, err) {
    this.port.postMessage({
      type: 'processor-error',
      message: `${prefix}: ${err?.stack || err?.message || String(err)}`,
    });
  }

  process(inputs, outputs) {
    try {
      const input = inputs[0] || [];
      const output = outputs[0] || [];
      const inL = input[0];
      const outL = output[0];
      const outR = output[1] || output[0];

      if (!outL || !outR) return true;

      // Debug bypass: direct monitor path to validate audio graph independently from WASM DSP.
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

      if (!this.ready) {
        outL.fill(0);
        outR.fill(0);
        return true;
      }

      // Fallback behavior if WASM binding is unavailable.
      if (!this.wasm || typeof this.wasm.processBlock !== 'function') {
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

      this.wasm.processBlock(input, output, this.metrics);
      return true;
    } catch (err) {
      this.postProcessorError('process() failed', err);
      return true;
    }
  }
}

registerProcessor('sound-bubbles-worklet', SoundBubblesWorklet);