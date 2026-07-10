import fs from 'node:fs';
import createBubbleCloudModule from '../../ui/web/bubble_cloud_wasm.js';

const PARAMS = {
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
  stereo_width: 27,
  attack_pan_spread: 28,
  sustain_pan_spread: 29,
  smart_start_enable: 30,
  smart_start_range: 31,
  envelope_variation: 32,
  envelope_family: 33,
  wet_drive: 34,
  wet_clip_amount: 35,
  wet_output_trim: 36,
  sustain_diffusion_enable: 37,
  sustain_diffusion_amount: 38,
  sustain_diffusion_stages: 39,
  sustain_diffusion_delay: 40,
  sustain_diffusion_feedback: 41,
  droplet_enable: 42,
  droplet_probability: 43,
  droplet_gain: 44,
  droplet_length_scale: 45,
  memory_mix: 46,
  memory_pull: 47,
  memory_darkening: 48,
  tone_variation: 49,
  attack_brightness: 50,
  sustain_darkness: 51,
  attack_rate_jitter: 52,
  attack_rate_jitter_depth: 53,
  quality_profile: 54,
  active_voice_limit: 55,
  freeze_amount: 56,
  freeze_enabled: 57,
  reverse_probability: 58,
  pitch_mode: 59,
  shimmer_amount: 60,
  final_limiter_ceiling_db: 61,
  final_limiter_release_ms: 62,
};

function readWavMono(path) {
  const buf = fs.readFileSync(path);
  if (buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE') {
    throw new Error('Input is not RIFF/WAVE');
  }
  let offset = 12;
  let fmt = null;
  let dataOffset = -1;
  let dataSize = 0;
  while (offset + 8 <= buf.length) {
    const id = buf.toString('ascii', offset, offset + 4);
    const size = buf.readUInt32LE(offset + 4);
    offset += 8;
    if (id === 'fmt ') {
      fmt = {
        audioFormat: buf.readUInt16LE(offset),
        channels: buf.readUInt16LE(offset + 2),
        sampleRate: buf.readUInt32LE(offset + 4),
        bitsPerSample: buf.readUInt16LE(offset + 14),
      };
    } else if (id === 'data') {
      dataOffset = offset;
      dataSize = size;
      break;
    }
    offset += size + (size & 1);
  }
  if (!fmt || dataOffset < 0) throw new Error('Missing fmt/data chunks');
  if (fmt.sampleRate !== 44100) throw new Error('Expected 44.1 kHz WAV');
  if (![1, 2].includes(fmt.channels)) throw new Error('Expected mono/stereo WAV');
  const bytesPerSample = fmt.bitsPerSample / 8;
  const frames = dataSize / (bytesPerSample * fmt.channels);
  const mono = new Float32Array(frames);
  for (let frame = 0; frame < frames; frame++) {
    let sum = 0;
    for (let ch = 0; ch < fmt.channels; ch++) {
      const sampleOffset = dataOffset + (frame * fmt.channels + ch) * bytesPerSample;
      if (fmt.audioFormat === 1 && fmt.bitsPerSample === 16) {
        sum += Math.max(-1, buf.readInt16LE(sampleOffset) / 32768);
      } else if (fmt.audioFormat === 3 && fmt.bitsPerSample === 32) {
        sum += buf.readFloatLE(sampleOffset);
      } else {
        throw new Error('Unsupported WAV sample format');
      }
    }
    mono[frame] = sum / fmt.channels;
  }
  return mono;
}

function rmsAndPeak(left, right, chunk) {
  let sumL = 0;
  let sumR = 0;
  let peakL = 0;
  let peakR = 0;
  for (let i = 0; i < chunk; i++) {
    const l = left[i];
    const r = right[i];
    sumL += l * l;
    sumR += r * r;
    peakL = Math.max(peakL, Math.abs(l));
    peakR = Math.max(peakR, Math.abs(r));
  }
  return [Math.sqrt(sumL / chunk), Math.sqrt(sumR / chunk), peakL, peakR];
}

const [wavPath, presetPath, metricsPath] = process.argv.slice(2);
if (!wavPath || !presetPath || !metricsPath) {
  throw new Error('Usage: node wasm_metrics_runner.mjs input.wav preset.json metrics.csv');
}

const module = await createBubbleCloudModule();
const requiredRuntime = ['HEAPF32', 'cwrap'];
const requiredExports = ['wasm_get_peak_l', 'wasm_get_peak_r', 'wasm_get_clip_count', 'wasm_get_limiter_gain'];
const missing = [
  ...requiredRuntime.filter((name) => module[name] === undefined),
  ...requiredExports.filter((name) => module[`_${name}`] === undefined),
];
if (missing.length > 0) {
  console.error(`WASM module is stale or missing test exports: ${missing.join(', ')}. Rebuild with make wasm.`);
  process.exit(1);
}
const wasmInit = module.cwrap('wasm_init', null, ['number']);
const wasmReset = module.cwrap('wasm_reset', null, []);
const wasmProcess = module.cwrap('wasm_process', null, ['number', 'number', 'number', 'number']);
const wasmSetParam = module.cwrap('wasm_set_param', null, ['number', 'number']);
const wasmAlloc = module.cwrap('wasm_alloc', 'number', ['number']);
const wasmFree = module.cwrap('wasm_free', null, ['number']);
const getEnvelope = module.cwrap('wasm_get_envelope', 'number', []);
const getState = module.cwrap('wasm_get_state', 'number', []);
const getActiveVoices = module.cwrap('wasm_get_active_voices', 'number', []);
const getPeakL = module.cwrap('wasm_get_peak_l', 'number', []);
const getPeakR = module.cwrap('wasm_get_peak_r', 'number', []);
const getClipCount = module.cwrap('wasm_get_clip_count', 'number', []);
const getLimiterGain = module.cwrap('wasm_get_limiter_gain', 'number', []);

wasmInit(44100.0);
const preset = JSON.parse(fs.readFileSync(presetPath, 'utf8'));
const updates = {};
for (const [key, paramId] of Object.entries(PARAMS)) {
  let value = preset[key];
  if (value === undefined) {
    if (key === 'mix_dry_gain') value = preset.master_dry_gain;
    else if (key === 'mix_wet_gain') value = preset.master_wet_gain;
    else if (key === 'attack_region_min_offset_samples') value = preset.micro_offset_samples;
    else if (key === 'attack_region_max_offset_samples') {
      value = (preset.micro_offset_samples ?? 441) + (preset.micro_jitter_samples ?? 3087);
    }
    else if (key === 'body_region_min_offset_samples') value = preset.short_offset_samples;
    else if (key === 'body_region_max_offset_samples') {
      value = (preset.short_offset_samples ?? 3528) + (preset.short_jitter_samples ?? 7497);
    }
    else if (key === 'memory_region_min_offset_samples') value = preset.body_offset_samples;
    else if (key === 'memory_region_max_offset_samples') {
      value = (preset.body_offset_samples ?? 11025) + (preset.body_jitter_samples ?? 28665);
    }
  }
  if (value !== undefined) {
    const numericValue = Number(value);
    if (!Number.isFinite(numericValue)) {
      throw new Error(`Preset parameter ${key} must be finite, got ${value}`);
    }
    updates[paramId] = numericValue;
  }
}
for (const [paramId, value] of Object.entries(updates)) {
  wasmSetParam(Number(paramId), value);
}
wasmReset();

const mono = readWavMono(wavPath);
const blockSize = 32;
const inPtr = wasmAlloc(blockSize * 4);
const outLPtr = wasmAlloc(blockSize * 4);
const outRPtr = wasmAlloc(blockSize * 4);
const rows = ['block,active_voices,engine_state,envelope,out_rms_l,out_rms_r,out_peak_l,out_peak_r,peak_l,peak_r,clip_count,limiter_gain'];

let block = 0;
for (let offset = 0; offset < mono.length; offset += blockSize) {
  const chunk = Math.min(blockSize, mono.length - offset);
  module.HEAPF32.set(mono.subarray(offset, offset + chunk), inPtr >> 2);
  wasmProcess(inPtr, outLPtr, outRPtr, chunk);
  const left = module.HEAPF32.slice(outLPtr >> 2, (outLPtr >> 2) + chunk);
  const right = module.HEAPF32.slice(outRPtr >> 2, (outRPtr >> 2) + chunk);
  const [outRmsL, outRmsR, outPeakL, outPeakR] = rmsAndPeak(left, right, chunk);
  rows.push([
    block,
    getActiveVoices(),
    getState(),
    getEnvelope(),
    outRmsL,
    outRmsR,
    outPeakL,
    outPeakR,
    getPeakL(),
    getPeakR(),
    getClipCount(),
    getLimiterGain(),
  ].map((value) => Number(value).toPrecision(9)).join(','));
  block += 1;
}

wasmFree(inPtr);
wasmFree(outLPtr);
wasmFree(outRPtr);
fs.writeFileSync(metricsPath, rows.join('\n') + '\n');
