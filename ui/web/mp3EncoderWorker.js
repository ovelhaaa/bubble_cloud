/* global importScripts */

const ENCODER_SCRIPT_CANDIDATES = [
  'vendor/lame.min.js',
  'https://cdn.jsdelivr.net/npm/lamejs@1.2.1/lame.min.js',
  'https://unpkg.com/lamejs@1.2.1/lame.min.js',
];

let encoderReady = false;
let encoderLoadError = '';

function ensureEncoderLoaded() {
  if (encoderReady && self.lamejs?.Mp3Encoder) return;

  let lastError = null;
  for (const url of ENCODER_SCRIPT_CANDIDATES) {
    try {
      importScripts(url);
      if (self.lamejs?.Mp3Encoder) {
        encoderReady = true;
        encoderLoadError = '';
        return;
      }
      lastError = new Error(`Script carregado sem Mp3Encoder: ${url}`);
    } catch (err) {
      lastError = err;
    }
  }

  encoderReady = false;
  encoderLoadError = lastError?.message || String(lastError) || 'Erro desconhecido ao carregar o encoder MP3.';
  throw new Error(`Não foi possível carregar o encoder MP3. ${encoderLoadError}`);
}

function floatTo16BitPCM(floatSamples) {
  const pcm = new Int16Array(floatSamples.length);
  for (let i = 0; i < floatSamples.length; i += 1) {
    const sample = Math.max(-1, Math.min(1, floatSamples[i]));
    pcm[i] = sample < 0 ? sample * 0x8000 : sample * 0x7fff;
  }
  return pcm;
}

self.onmessage = (event) => {
  const data = event.data || {};
  if (data.type !== 'encode-mp3') return;

  const { requestId, sampleRate, kbps = 192, leftBuffer, rightBuffer } = data;

  try {
    ensureEncoderLoaded();
    const LameCtor = self.lamejs?.Mp3Encoder;
    if (!LameCtor) throw new Error('Encoder MP3 indisponível no worker.');

    const leftFloat = new Float32Array(leftBuffer);
    const rightFloat = new Float32Array(rightBuffer);
    const left = floatTo16BitPCM(leftFloat);
    const right = floatTo16BitPCM(rightFloat);

    const encoder = new LameCtor(2, sampleRate, kbps);
    const blockSize = 1152;
    const chunks = [];

    for (let i = 0; i < left.length; i += blockSize) {
      const leftChunk = left.subarray(i, i + blockSize);
      const rightChunk = right.subarray(i, i + blockSize);
      const mp3buf = encoder.encodeBuffer(leftChunk, rightChunk);
      if (mp3buf.length > 0) chunks.push(new Uint8Array(mp3buf));
    }

    const flush = encoder.flush();
    if (flush.length > 0) chunks.push(new Uint8Array(flush));

    const totalLength = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
    const merged = new Uint8Array(totalLength);
    let offset = 0;
    chunks.forEach((chunk) => {
      merged.set(chunk, offset);
      offset += chunk.length;
    });

    self.postMessage({ type: 'success', requestId, mp3Buffer: merged.buffer }, [merged.buffer]);
  } catch (err) {
    self.postMessage({
      type: 'error',
      requestId,
      message: err?.message || String(err),
      details: {
        encoderLoadError,
        sourcesTried: ENCODER_SCRIPT_CANDIDATES,
      },
    });
  }
};
