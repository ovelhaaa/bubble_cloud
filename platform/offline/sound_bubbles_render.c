#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "sound_bubbles_dsp.h"

// --- 1. Design Summary ---
// This file is a minimal, self-contained CLI renderer for the Sound Bubbles DSP core.
// It loads an input WAV file, a JSON preset, applies the DSP effect, and writes an output WAV file.
// All audio data is read into memory at once to keep the implementation simple.

// --- 2. WAV Format Assumptions ---
// Input: 44100 Hz, 1 or 2 channels, 16-bit PCM or 32-bit IEEE Float.
// Output: 44100 Hz, 2 channels, 32-bit IEEE Float.

// --- 3. Preset JSON Assumptions ---
// The JSON file must be a flat object containing numeric values.
// Keys are matched via simple string searching (`strstr`). Missing keys will leave default values.

// --- 4. CLI Behavior ---
// Usage: ./sound_bubbles_render input.wav preset.json output.wav

// --- 5. Error Handling Policy ---
// The program will print a descriptive error to stderr and return a non-zero exit code
// for invalid arguments, file I/O errors, unsupported WAV formats, or memory allocation failures.

// --- WAV Header Structures ---

// We only use this to build the exact output header now.
// For input, we will robustly scan chunks.
#pragma pack(push, 1)
typedef struct {
    char chunkId[4];       // "RIFF"
    uint32_t chunkSize;
    char format[4];        // "WAVE"
    char subchunk1Id[4];   // "fmt "
    uint32_t subchunk1Size;
    uint16_t audioFormat;  // 1 = PCM, 3 = IEEE Float
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} WavHeader;
#pragma pack(pop)

typedef struct {
    float* data;
    uint32_t numSamples; // In frames
    uint16_t numChannels;
    uint32_t sampleRate;
} WavData;

// --- Helper Functions ---

static void PrintUsage(const char* progName) {
    fprintf(stderr,
        "Usage:\n"
        "  %s <input.wav> <preset.json> <output.wav> [--metrics-out <metrics.csv>] [--repro-check]\n"
        "  %s compare <reference_metrics.csv> <candidate_metrics.csv> [--max-threshold <value>] [--mean-threshold <value>]\n",
        progName,
        progName);
}

static void ErrorExit(const char* message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

// Minimal manual JSON parser for flat numeric keys, enhanced to avoid string contents
static const char* FindJsonKey(const char* json_str, const char* key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    size_t key_len = strlen(search_key);

    const char* pos = json_str;
    while ((pos = strstr(pos, search_key)) != NULL) {
        // Look ahead to ensure the matched string is used as a JSON key (followed by ':')
        const char* check_pos = pos + key_len;
        while (*check_pos == ' ' || *check_pos == '\t' || *check_pos == '\r' || *check_pos == '\n') {
            check_pos++;
        }
        if (*check_pos == ':') {
            return check_pos + 1; // Return the position immediately after the colon
        }
        pos += key_len; // Continue searching
    }
    return NULL;
}

static float GetJsonFloat(const char* json_str, const char* key, float default_val) {
    const char* pos = FindJsonKey(json_str, key);
    if (!pos) return default_val;

    float val = default_val;
    if (sscanf(pos, " %f", &val) == 1) {
        return val;
    }
    return default_val;
}

static int32_t GetJsonInt(const char* json_str, const char* key, int32_t default_val) {
    const char* pos = FindJsonKey(json_str, key);
    if (!pos) return default_val;

    int32_t val = default_val;
    if (sscanf(pos, " %d", &val) == 1) {
        return val;
    }
    return default_val;
}

static float GetJsonFloatLegacyFallback(const char* json_str, const char* canonical_key, const char* legacy_key, float default_val) {
    const char* canonical_pos = FindJsonKey(json_str, canonical_key);
    if (canonical_pos) {
        float val = default_val;
        if (sscanf(canonical_pos, " %f", &val) == 1) {
            return val;
        }
    }
    return GetJsonFloat(json_str, legacy_key, default_val);
}

// --- Validation Functions ---
static void ValidateConfig(const EngineConfig_t* config, float master_dry, float master_wet) {
    if (config->density_burst < 0.0f || config->density_sustain < 0.0f || config->density_decay < 0.0f) {
        ErrorExit("Preset validation failed: densities must be >= 0.");
    }
    if (config->burst_duration_ticks < 0 || config->burst_immediate_count < 0) {
        ErrorExit("Preset validation failed: burst_duration_ticks and burst_immediate_count must be >= 0.");
    }
    if (config->attack_region.min_offset_samples < 0 || config->attack_region.max_offset_samples < 0 ||
        config->body_region.min_offset_samples < 0 || config->body_region.max_offset_samples < 0 ||
        config->memory_region.min_offset_samples < 0 || config->memory_region.max_offset_samples < 0) {
        ErrorExit("Preset validation failed: region offsets must be >= 0.");
    }

    if (!isfinite(config->duck_burst_level) || config->duck_burst_level < 0.0f || config->duck_burst_level > 1.0f) {
        ErrorExit("Preset validation failed: duck_burst_level must be between 0.0 and 1.0.");
    }
    if (!isfinite(config->duck_attack_coef) || config->duck_attack_coef < 0.0f || config->duck_attack_coef > 1.0f) {
        ErrorExit("Preset validation failed: duck_attack_coef must be between 0.0 and 1.0.");
    }
    if (!isfinite(config->duck_release_coef) || config->duck_release_coef < 0.0f || config->duck_release_coef > 1.0f) {
        ErrorExit("Preset validation failed: duck_release_coef must be between 0.0 and 1.0.");
    }

    if (!isfinite(master_dry) || master_dry < 0.0f || !isfinite(master_wet) || master_wet < 0.0f) {
        ErrorExit("Preset validation failed: master_dry and master_wet must be finite and >= 0.");
    }

    for (int i = 0; i < BUBBLE_CLASS_COUNT; i++) {
        const BubbleClassConfig_t* c = &config->class_configs[i];
        if (c->duration_ms_min <= 0.0f || c->duration_ms_max <= 0.0f) {
            ErrorExit("Preset validation failed: class durations must be > 0.");
        }
    }
}

typedef struct {
    uint32_t block_index;
    int32_t spawn_count;
    int32_t active_voices;
    int32_t engine_state;
    float ducking_gain;
    float envelope;
    float out_rms_l;
    float out_rms_r;
    float out_peak_l;
    float out_peak_r;
} MetricRow_t;

typedef struct {
    SoundBubblesBlockMetrics_t last_control_metrics;
    uint32_t callback_count;
} MetricsCallbackState_t;

typedef struct {
    uint64_t hash;
    uint32_t block_count;
} MetricsDigest_t;

static uint64_t Fnv1a64UpdateU32(uint64_t h, uint32_t v) {
    const uint64_t prime = 1099511628211ull;
    for (int i = 0; i < 4; ++i) {
        uint8_t b = (uint8_t)((v >> (i * 8)) & 0xFFu);
        h ^= b;
        h *= prime;
    }
    return h;
}

static uint64_t Fnv1a64UpdateI32(uint64_t h, int32_t v) {
    return Fnv1a64UpdateU32(h, (uint32_t)v);
}

static uint64_t Fnv1a64UpdateF32(uint64_t h, float v) {
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return Fnv1a64UpdateU32(h, bits.u);
}

static uint64_t HashMetricRow(uint64_t hash, const MetricRow_t* row) {
    hash = Fnv1a64UpdateU32(hash, row->block_index);
    hash = Fnv1a64UpdateI32(hash, row->spawn_count);
    hash = Fnv1a64UpdateI32(hash, row->active_voices);
    hash = Fnv1a64UpdateI32(hash, row->engine_state);
    hash = Fnv1a64UpdateF32(hash, row->ducking_gain);
    hash = Fnv1a64UpdateF32(hash, row->envelope);
    hash = Fnv1a64UpdateF32(hash, row->out_rms_l);
    hash = Fnv1a64UpdateF32(hash, row->out_rms_r);
    hash = Fnv1a64UpdateF32(hash, row->out_peak_l);
    hash = Fnv1a64UpdateF32(hash, row->out_peak_r);
    return hash;
}

static void MetricsCallback(const SoundBubblesBlockMetrics_t* metrics, void* user_data) {
    MetricsCallbackState_t* state = (MetricsCallbackState_t*)user_data;
    state->last_control_metrics = *metrics;
    state->callback_count++;
}

static void ComputeOutputEnergy(const float* out_left, const float* out_right, uint32_t chunk, float* rms_l, float* rms_r, float* peak_l, float* peak_r) {
    float sum_sq_l = 0.0f;
    float sum_sq_r = 0.0f;
    *peak_l = 0.0f;
    *peak_r = 0.0f;
    for (uint32_t i = 0; i < chunk; ++i) {
        float l = out_left[i];
        float r = out_right[i];
        float abs_l = fabsf(l);
        float abs_r = fabsf(r);
        sum_sq_l += l * l;
        sum_sq_r += r * r;
        if (abs_l > *peak_l) *peak_l = abs_l;
        if (abs_r > *peak_r) *peak_r = abs_r;
    }
    *rms_l = sqrtf(sum_sq_l / (float)chunk);
    *rms_r = sqrtf(sum_sq_r / (float)chunk);
}

static FILE* OpenMetricsLog(const char* metrics_file) {
    if (metrics_file == NULL) return NULL;
    FILE* f = fopen(metrics_file, "w");
    if (!f) {
        ErrorExit("Failed to open metrics output file.");
    }
    fprintf(f, "block,spawn_count,active_voices,engine_state,ducking_gain,envelope,out_rms_l,out_rms_r,out_peak_l,out_peak_r\n");
    return f;
}

// --- WAV I/O ---

static WavData ReadWav(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) ErrorExit("Could not open input WAV file.");

    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];

    if (fread(riff_id, 1, 4, file) != 4 ||
        fread(&riff_size, 4, 1, file) != 1 ||
        fread(wave_id, 1, 4, file) != 4) {
        fclose(file);
        ErrorExit("Failed to read root RIFF header.");
    }

    if (strncmp(riff_id, "RIFF", 4) != 0 || strncmp(wave_id, "WAVE", 4) != 0) {
        fclose(file);
        ErrorExit("Input file is not a valid RIFF/WAVE file.");
    }

    bool fmt_found = false;
    bool data_found = false;

    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataSize = 0;
    long dataOffset = 0;

    // Iterate through chunks
    char chunkId[4];
    uint32_t chunkSize;

    while (fread(chunkId, 1, 4, file) == 4 && fread(&chunkSize, 4, 1, file) == 1) {
        if (strncmp(chunkId, "fmt ", 4) == 0) {
            fmt_found = true;
            if (chunkSize < 16) {
                fclose(file);
                ErrorExit("Invalid fmt chunk size.");
            }
            if (fread(&audioFormat, 2, 1, file) != 1 ||
                fread(&numChannels, 2, 1, file) != 1 ||
                fread(&sampleRate, 4, 1, file) != 1 ||
                fseek(file, 6, SEEK_CUR) != 0 || // skip byteRate and blockAlign
                fread(&bitsPerSample, 2, 1, file) != 1) {
                fclose(file);
                ErrorExit("Failed to read fmt chunk.");
            }
            // Skip any remaining bytes in the fmt chunk (e.g. cbSize extensions)
            fseek(file, chunkSize - 16, SEEK_CUR);
        } else if (strncmp(chunkId, "data", 4) == 0) {
            data_found = true;
            dataSize = chunkSize;
            dataOffset = ftell(file);
            break; // Stop parsing chunks once we find data
        } else {
            // Skip unknown chunk
            fseek(file, chunkSize, SEEK_CUR);
        }

        // RIFF pads odd-sized chunks with a single null byte
        if (chunkSize % 2 != 0) {
            fseek(file, 1, SEEK_CUR);
        }
    }

    if (!fmt_found || !data_found) {
        fclose(file);
        ErrorExit("Input WAV file must contain 'fmt ' and 'data' chunks.");
    }

    if (sampleRate != BUBBLES_SAMPLE_RATE) {
        fclose(file);
        ErrorExit("Input WAV must be 44.1 kHz.");
    }

    if (numChannels != 1 && numChannels != 2) {
        fclose(file);
        ErrorExit("Input WAV must be Mono or Stereo.");
    }

    if (audioFormat != 1 && audioFormat != 3) {
        fclose(file);
        ErrorExit("Unsupported audio format (must be PCM or IEEE Float).");
    }

    // Seek back to start of data chunk
    fseek(file, dataOffset, SEEK_SET);

    uint32_t numSamplesTotal = 0;
    if (bitsPerSample == 16) {
        if (dataSize % 2 != 0) {
             fclose(file);
             ErrorExit("PCM16 data size is not a multiple of 2.");
        }
        numSamplesTotal = dataSize / 2;
    } else if (bitsPerSample == 32) {
        if (dataSize % 4 != 0) {
             fclose(file);
             ErrorExit("Float32 data size is not a multiple of 4.");
        }
        numSamplesTotal = dataSize / 4;
    } else {
        fclose(file);
        ErrorExit("Unsupported bit depth (must be 16-bit PCM or 32-bit Float).");
    }

    uint32_t numFrames = numSamplesTotal / numChannels;

    WavData wav;
    wav.numSamples = numFrames;
    wav.numChannels = numChannels;
    wav.sampleRate = sampleRate;
    wav.data = (float*)malloc(numFrames * wav.numChannels * sizeof(float));

    if (!wav.data) {
        fclose(file);
        ErrorExit("Failed to allocate memory for input WAV data.");
    }

    if (audioFormat == 1 && bitsPerSample == 16) {
        int16_t* pcmData = (int16_t*)malloc(dataSize);
        if (!pcmData || fread(pcmData, 1, dataSize, file) != dataSize) {
            free(pcmData);
            free(wav.data);
            fclose(file);
            ErrorExit("Failed to read PCM data.");
        }
        for (uint32_t i = 0; i < numSamplesTotal; i++) {
            wav.data[i] = (float)pcmData[i] / 32768.0f;
        }
        free(pcmData);
    } else if (audioFormat == 3 && bitsPerSample == 32) {
        if (fread(wav.data, 1, dataSize, file) != dataSize) {
            free(wav.data);
            fclose(file);
            ErrorExit("Failed to read Float data.");
        }
    }

    fclose(file);
    return wav;
}

static void WriteWav(const char* filename, const float* left, const float* right, uint32_t numFrames) {
    FILE* file = fopen(filename, "wb");
    if (!file) ErrorExit("Could not open output WAV file for writing.");

    uint32_t bytesPerSample = 4; // 32-bit float
    uint32_t numChannels = 2;    // Stereo
    uint32_t byteRate = BUBBLES_SAMPLE_RATE * numChannels * bytesPerSample;
    uint32_t blockAlign = numChannels * bytesPerSample;
    uint32_t dataSize = numFrames * blockAlign;

    WavHeader header = {
        {'R', 'I', 'F', 'F'},
        36 + dataSize,
        {'W', 'A', 'V', 'E'},
        {'f', 'm', 't', ' '},
        16, // PCM/Float format chunk size
        3,  // IEEE Float
        (uint16_t)numChannels,
        BUBBLES_SAMPLE_RATE,
        byteRate,
        (uint16_t)blockAlign,
        (uint16_t)(bytesPerSample * 8)
    };

    fwrite(&header, sizeof(WavHeader), 1, file);

    const char dataChunkId[4] = {'d', 'a', 't', 'a'};
    fwrite(dataChunkId, 1, 4, file);
    fwrite(&dataSize, 4, 1, file);

    // Interleave float data
    float* interleaved = (float*)malloc(numFrames * 2 * sizeof(float));
    if (!interleaved) {
        fclose(file);
        ErrorExit("Failed to allocate memory for output interleaving.");
    }

    for (uint32_t i = 0; i < numFrames; i++) {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }

    fwrite(interleaved, sizeof(float), numFrames * 2, file);

    free(interleaved);
    fclose(file);
}

// --- Parsing ---

static void LoadPreset(const char* filename, EngineConfig_t* config, float* master_dry, float* master_wet) {
    FILE* file = fopen(filename, "rb");
    if (!file) ErrorExit("Could not open JSON preset file.");

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* json_str = (char*)malloc(length + 1);
    if (!json_str) {
        fclose(file);
        ErrorExit("Failed to allocate memory for JSON parsing.");
    }

    if (fread(json_str, 1, length, file) != (size_t)length) {
        free(json_str);
        fclose(file);
        ErrorExit("Failed to read JSON preset file.");
    }
    json_str[length] = '\0';
    fclose(file);

    // Parse core parameters
    config->noise_floor = GetJsonFloat(json_str, "noise_floor", 0.001f);
    config->tracking_thresh = GetJsonFloat(json_str, "tracking_thresh", 0.01f);
    config->sustain_thresh = GetJsonFloat(json_str, "sustain_thresh", 0.1f);
    config->transient_delta = GetJsonFloat(json_str, "transient_delta", 0.05f);

    config->duck_burst_level = GetJsonFloat(json_str, "duck_burst_level", 0.2f);
    config->duck_attack_coef = GetJsonFloat(json_str, "duck_attack_coef", 0.99f);
    config->duck_release_coef = GetJsonFloat(json_str, "duck_release_coef", 0.999f);

    config->burst_duration_ticks = GetJsonInt(json_str, "burst_duration_ticks", 10);
    config->burst_immediate_count = GetJsonInt(json_str, "burst_immediate_count", 3);

    config->density_burst = GetJsonFloat(json_str, "density_burst", 50.0f);
    config->density_sustain = GetJsonFloat(json_str, "density_sustain", 15.0f);
    config->density_decay = GetJsonFloat(json_str, "density_decay", 5.0f);

    // Canonical schema (v2):
    //  - attack/body/memory semantic regions:
    //      attack_region_min_offset_samples
    //      attack_region_max_offset_samples
    //      body_region_min_offset_samples
    //      body_region_max_offset_samples
    //      memory_region_min_offset_samples
    //      memory_region_max_offset_samples
    //  - class durations:
    //      micro_duration_ms_min/max
    //      short_duration_ms_min/max
    //      body_duration_ms_min/max
    //  - dynamics/shaping:
    //      density_*, duck_*, burst_*
    //  - randomness/mix:
    //      rng_seed, mix_dry_gain, mix_wet_gain
    //
    // Backward compatibility layer:
    //  - legacy offset+jitter keys are still accepted and translated to regions:
    //      micro_offset_samples + micro_jitter_samples        -> attack_region_[min/max]
    //      short_offset_samples + short_jitter_samples        -> body_region_[min/max]
    //      body_offset_samples + body_jitter_samples          -> memory_region_[min/max]
    //  - legacy output mix keys remain accepted:
    //      master_dry_gain / master_wet_gain
    //
    // Canonical keys always take precedence when both forms are present.
    int32_t micro_offset = GetJsonInt(json_str, "micro_offset_samples", 441);
    int32_t micro_jitter = GetJsonInt(json_str, "micro_jitter_samples", 3087);
    int32_t short_offset = GetJsonInt(json_str, "short_offset_samples", 3528);
    int32_t short_jitter = GetJsonInt(json_str, "short_jitter_samples", 7497);
    int32_t body_offset = GetJsonInt(json_str, "body_offset_samples", 11025);
    int32_t body_jitter = GetJsonInt(json_str, "body_jitter_samples", 28665);

    config->attack_region.min_offset_samples = GetJsonInt(json_str, "attack_region_min_offset_samples", micro_offset);
    config->attack_region.max_offset_samples = GetJsonInt(json_str, "attack_region_max_offset_samples", micro_offset + micro_jitter);
    config->body_region.min_offset_samples = GetJsonInt(json_str, "body_region_min_offset_samples", short_offset);
    config->body_region.max_offset_samples = GetJsonInt(json_str, "body_region_max_offset_samples", short_offset + short_jitter);
    config->memory_region.min_offset_samples = GetJsonInt(json_str, "memory_region_min_offset_samples", body_offset);
    config->memory_region.max_offset_samples = GetJsonInt(json_str, "memory_region_max_offset_samples", body_offset + body_jitter);

    config->rng_seed = (uint32_t)GetJsonInt(json_str, "rng_seed", 1);

    // Micro Attack
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = GetJsonFloat(json_str, "micro_duration_ms_min", 5.0f);
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = GetJsonFloat(json_str, "micro_duration_ms_max", 15.0f);
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    // Short Intermediate
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = GetJsonFloat(json_str, "short_duration_ms_min", 20.0f);
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = GetJsonFloat(json_str, "short_duration_ms_max", 50.0f);
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    // Sustain Body
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = GetJsonFloat(json_str, "body_duration_ms_min", 80.0f);
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = GetJsonFloat(json_str, "body_duration_ms_max", 200.0f);
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;

    // Canonical mix keys with legacy fallback.
    *master_dry = GetJsonFloatLegacyFallback(json_str, "mix_dry_gain", "master_dry_gain", 1.0f);
    *master_wet = GetJsonFloatLegacyFallback(json_str, "mix_wet_gain", "master_wet_gain", 1.0f);

    free(json_str);
}

static MetricsDigest_t RenderWithMetrics(const EngineConfig_t* config, float master_dry, float master_wet, const float* mono_in, uint32_t num_frames, float* out_left, float* out_right, FILE* metrics_log) {
    MetricsDigest_t digest = {0};
    digest.hash = 1469598103934665603ull;
    digest.block_count = 0;

    int16_t* delay_buffer = (int16_t*)calloc(BUBBLES_BUFFER_SIZE_SAMPLES, sizeof(int16_t));
    if (!delay_buffer) {
        ErrorExit("Failed to allocate DSP delay buffer.");
    }

    SoundBubblesEngine_t engine;
    SoundBubbles_Init(&engine, delay_buffer, config);
    engine.master_dry_gain = master_dry;
    engine.master_wet_gain = master_wet;

    MetricsCallbackState_t metrics_state = {0};
    SoundBubbles_SetMetricsCallback(&engine, MetricsCallback, &metrics_state);

    uint32_t processed = 0;
    while (processed < num_frames) {
        uint32_t chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > num_frames) {
            chunk = num_frames - processed;
        }
        uint32_t callback_count_before = metrics_state.callback_count;
        SoundBubbles_ProcessBlock(&engine, &mono_in[processed], &out_left[processed], &out_right[processed], chunk);
        processed += chunk;

        if (metrics_state.callback_count == callback_count_before) {
            continue;
        }

        MetricRow_t row = {0};
        row.block_index = digest.block_count;
        row.spawn_count = metrics_state.last_control_metrics.spawn_count;
        row.active_voices = metrics_state.last_control_metrics.active_voices;
        row.engine_state = metrics_state.last_control_metrics.engine_state;
        row.ducking_gain = metrics_state.last_control_metrics.ducking_gain;
        row.envelope = metrics_state.last_control_metrics.envelope;
        ComputeOutputEnergy(&out_left[processed - chunk], &out_right[processed - chunk], chunk, &row.out_rms_l, &row.out_rms_r, &row.out_peak_l, &row.out_peak_r);
        digest.hash = HashMetricRow(digest.hash, &row);
        digest.block_count++;

        if (metrics_log != NULL) {
            fprintf(metrics_log, "%u,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                row.block_index,
                row.spawn_count,
                row.active_voices,
                row.engine_state,
                row.ducking_gain,
                row.envelope,
                row.out_rms_l,
                row.out_rms_r,
                row.out_peak_l,
                row.out_peak_r);
        }
    }

    free(delay_buffer);
    return digest;
}

static void CompareMetricsFiles(const char* ref_file, const char* cand_file, double max_threshold, double mean_threshold) {
    FILE* ref = fopen(ref_file, "r");
    FILE* cand = fopen(cand_file, "r");
    if (!ref || !cand) {
        if (ref) fclose(ref);
        if (cand) fclose(cand);
        ErrorExit("Failed to open metrics file for compare mode.");
    }

    char ref_line[1024];
    char cand_line[1024];
    if (!fgets(ref_line, sizeof(ref_line), ref) || !fgets(cand_line, sizeof(cand_line), cand)) {
        fclose(ref);
        fclose(cand);
        ErrorExit("Metrics files must include a header row.");
    }

    double totals[9] = {0};
    double maxes[9] = {0};
    const char* labels[9] = {
        "spawn_count", "active_voices", "engine_state", "ducking_gain", "envelope",
        "out_rms_l", "out_rms_r", "out_peak_l", "out_peak_r"
    };
    uint32_t rows = 0;

    while (true) {
        char* ref_ok = fgets(ref_line, sizeof(ref_line), ref);
        char* cand_ok = fgets(cand_line, sizeof(cand_line), cand);
        if (!ref_ok && !cand_ok) break;
        if (!ref_ok || !cand_ok) {
            fclose(ref);
            fclose(cand);
            ErrorExit("Metrics row count mismatch between reference and candidate logs.");
        }

        unsigned int block_ref = 0;
        unsigned int block_cand = 0;
        double ref_vals[9] = {0};
        double cand_vals[9] = {0};
        int ref_fields = sscanf(ref_line, "%u,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
            &block_ref, &ref_vals[0], &ref_vals[1], &ref_vals[2], &ref_vals[3], &ref_vals[4], &ref_vals[5], &ref_vals[6], &ref_vals[7], &ref_vals[8]);
        int cand_fields = sscanf(cand_line, "%u,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
            &block_cand, &cand_vals[0], &cand_vals[1], &cand_vals[2], &cand_vals[3], &cand_vals[4], &cand_vals[5], &cand_vals[6], &cand_vals[7], &cand_vals[8]);
        if (ref_fields != 10 || cand_fields != 10) {
            fclose(ref);
            fclose(cand);
            ErrorExit("Invalid metrics CSV row format.");
        }
        if (block_ref != block_cand) {
            fclose(ref);
            fclose(cand);
            ErrorExit("Metrics block indices are misaligned.");
        }

        for (int i = 0; i < 9; ++i) {
            double d = fabs(ref_vals[i] - cand_vals[i]);
            totals[i] += d;
            if (d > maxes[i]) maxes[i] = d;
        }
        rows++;
    }

    fclose(ref);
    fclose(cand);

    if (rows == 0) {
        ErrorExit("Metrics files contain no data rows.");
    }

    bool pass = true;
    printf("Metric deltas for %u blocks:\n", rows);
    for (int i = 0; i < 9; ++i) {
        double mean = totals[i] / (double)rows;
        printf("  %-14s max=%.9g mean=%.9g\n", labels[i], maxes[i], mean);
        if (maxes[i] > max_threshold || mean > mean_threshold) {
            pass = false;
        }
    }
    printf("Thresholds: max<=%.9g mean<=%.9g\n", max_threshold, mean_threshold);
    if (!pass) {
        ErrorExit("Compare mode failed: one or more metric deltas exceeded thresholds.");
    }
    printf("Compare mode passed.\n");
}

// --- Main Execution ---

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "compare") == 0) {
        if (argc < 4 || argc > 8) {
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
        const char* ref_metrics = argv[2];
        const char* cand_metrics = argv[3];
        double max_threshold = 1e-6;
        double mean_threshold = 1e-7;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--max-threshold") == 0 && i + 1 < argc) {
                max_threshold = atof(argv[++i]);
            } else if (strcmp(argv[i], "--mean-threshold") == 0 && i + 1 < argc) {
                mean_threshold = atof(argv[++i]);
            } else {
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        CompareMetricsFiles(ref_metrics, cand_metrics, max_threshold, mean_threshold);
        return EXIT_SUCCESS;
    }

    if (argc < 4) {
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* input_wav_file = argv[1];
    const char* preset_json_file = argv[2];
    const char* output_wav_file = argv[3];
    const char* metrics_out_file = NULL;
    bool repro_check = false;

    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--metrics-out") == 0 && i + 1 < argc) {
            metrics_out_file = argv[++i];
        } else if (strcmp(argv[i], "--repro-check") == 0) {
            repro_check = true;
        } else {
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Load DSP Configuration
    EngineConfig_t config = {0};
    float master_dry = 1.0f;
    float master_wet = 1.0f;
    LoadPreset(preset_json_file, &config, &master_dry, &master_wet);
    ValidateConfig(&config, master_dry, master_wet);

    // Read Input Audio
    WavData in_wav = ReadWav(input_wav_file);
    uint32_t num_frames = in_wav.numSamples;

    // Convert stereo to mono for input if necessary
    float* mono_in = (float*)malloc(num_frames * sizeof(float));
    if (!mono_in) {
        free(in_wav.data);
        ErrorExit("Failed to allocate memory for mono conversion.");
    }

    if (in_wav.numChannels == 2) {
        for (uint32_t i = 0; i < num_frames; i++) {
            mono_in[i] = 0.5f * (in_wav.data[i * 2] + in_wav.data[i * 2 + 1]);
        }
    } else {
        memcpy(mono_in, in_wav.data, num_frames * sizeof(float));
    }
    free(in_wav.data);

    // Prepare Output Buffers
    float* out_left = (float*)malloc(num_frames * sizeof(float));
    float* out_right = (float*)malloc(num_frames * sizeof(float));
    if (!out_left || !out_right) {
        free(mono_in);
        if (out_left) free(out_left);
        if (out_right) free(out_right);
        ErrorExit("Failed to allocate memory for output buffers.");
    }

    FILE* metrics_log = OpenMetricsLog(metrics_out_file);
    MetricsDigest_t digest = RenderWithMetrics(&config, master_dry, master_wet, mono_in, num_frames, out_left, out_right, metrics_log);
    if (metrics_log != NULL) {
        fclose(metrics_log);
    }

    // Final Validation of Rendered Audio
    for (uint32_t i = 0; i < num_frames; i++) {
        if (!isfinite(out_left[i]) || !isfinite(out_right[i])) {
            ErrorExit("Render validation failed: encountered NaN or infinite samples in the output buffer.");
        }
    }

    // Write Output Audio
    WriteWav(output_wav_file, out_left, out_right, num_frames);

    if (repro_check) {
        float* out_left_2 = (float*)malloc(num_frames * sizeof(float));
        float* out_right_2 = (float*)malloc(num_frames * sizeof(float));
        if (!out_left_2 || !out_right_2) {
            free(mono_in);
            free(out_left);
            free(out_right);
            if (out_left_2) free(out_left_2);
            if (out_right_2) free(out_right_2);
            ErrorExit("Failed to allocate buffers for reproducibility check.");
        }
        MetricsDigest_t second = RenderWithMetrics(&config, master_dry, master_wet, mono_in, num_frames, out_left_2, out_right_2, NULL);
        if (digest.hash != second.hash || digest.block_count != second.block_count) {
            free(out_left_2);
            free(out_right_2);
            free(mono_in);
            free(out_left);
            free(out_right);
            ErrorExit("Deterministic reproducibility check failed: metric hash mismatch.");
        }
        printf("Reproducibility check passed: block_count=%u metric_hash=%" PRIu64 "\n", digest.block_count, digest.hash);
        free(out_left_2);
        free(out_right_2);
    } else {
        printf("Metric digest: block_count=%u metric_hash=%" PRIu64 "\n", digest.block_count, digest.hash);
    }

    // Cleanup
    free(mono_in);
    free(out_left);
    free(out_right);

    printf("Successfully rendered %u frames to %s\n", num_frames, output_wav_file);
    return EXIT_SUCCESS;
}
