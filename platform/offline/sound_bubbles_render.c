#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
    fprintf(stderr, "Usage: %s <input.wav> <preset.json> <output.wav>\n", progName);
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

    // Semantic read regions (samples behind write head). New keys first, with
    // fallback to legacy offset+jitter fields for backward-compatible presets.
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

    // Output Mix Macros
    *master_dry = GetJsonFloat(json_str, "master_dry_gain", 1.0f);
    *master_wet = GetJsonFloat(json_str, "master_wet_gain", 1.0f);

    free(json_str);
}

// --- Main Execution ---

int main(int argc, char** argv) {
    if (argc != 4) {
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* input_wav_file = argv[1];
    const char* preset_json_file = argv[2];
    const char* output_wav_file = argv[3];

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

    // Initialize DSP Core
    int16_t* delay_buffer = (int16_t*)calloc(BUBBLES_BUFFER_SIZE_SAMPLES, sizeof(int16_t));
    if (!delay_buffer) {
        free(mono_in);
        free(out_left);
        free(out_right);
        ErrorExit("Failed to allocate DSP delay buffer.");
    }

    SoundBubblesEngine_t engine;
    SoundBubbles_Init(&engine, delay_buffer, &config);
    engine.master_dry_gain = master_dry;
    engine.master_wet_gain = master_wet;

    // Process Block by Block
    uint32_t processed = 0;
    while (processed < num_frames) {
        uint32_t chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > num_frames) {
            chunk = num_frames - processed;
        }

        SoundBubbles_ProcessBlock(&engine, &mono_in[processed], &out_left[processed], &out_right[processed], chunk);
        processed += chunk;
    }

    // Final Validation of Rendered Audio
    for (uint32_t i = 0; i < num_frames; i++) {
        if (!isfinite(out_left[i]) || !isfinite(out_right[i])) {
            ErrorExit("Render validation failed: encountered NaN or infinite samples in the output buffer.");
        }
    }

    // Write Output Audio
    WriteWav(output_wav_file, out_left, out_right, num_frames);

    // Cleanup
    free(mono_in);
    free(out_left);
    free(out_right);
    free(delay_buffer);

    printf("Successfully rendered %u frames to %s\n", num_frames, output_wav_file);
    return EXIT_SUCCESS;
}
